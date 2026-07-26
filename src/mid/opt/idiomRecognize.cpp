#include "../../include/mid/opt/idiomRecognize.hpp"

#include "../../include/mid/analysis/recurrenceAnalysis.hpp"
#include "../../include/mid/ir/irBuilder.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <climits>
#include <set>
#include <vector>

namespace {

bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks) {
    if (dynamic_cast<Constant *>(val) || dynamic_cast<Argument *>(val) ||
        dynamic_cast<GlobalVariable *>(val))
        return true;
    auto *inst = dynamic_cast<Instruction *>(val);
    return inst && !blocks.count(inst->parent_);
}

bool getLoopPhiIncoming(const Loop &loop, PhiInst *phi, Value *&init,
                        Value *&latchValue, BasicBlock *&latchBlock) {
    init = nullptr;
    latchValue = nullptr;
    latchBlock = nullptr;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!pred) return false;
        if (loop.blocks.count(pred)) {
            if (latchValue) return false;
            latchValue = phi->get_operand(i);
            latchBlock = pred;
        } else {
            if (init) return false;
            init = phi->get_operand(i);
        }
    }
    return init && latchValue && latchBlock;
}

int typeSize(Type *type) {
    if (!type) return -1;
    switch (type->tid_) {
    case Type::IntegerTyID:
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *array = static_cast<ArrayType *>(type);
        int element = typeSize(array->contained_);
        return element < 0 ? -1 : element * static_cast<int>(array->num_elements_);
    }
    default:
        return -1;
    }
}

bool matchIVPlusConstant(Value *value, PhiInst *iv, int &offset) {
    if (value == iv) {
        offset = 0;
        return true;
    }
    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (!bin) return false;
    if (bin->is_add()) {
        if (bin->get_operand(0) == iv) {
            auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(1));
            if (!c) return false;
            offset = c->value_;
            return true;
        }
        if (bin->get_operand(1) == iv) {
            auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(0));
            if (!c) return false;
            offset = c->value_;
            return true;
        }
    }
    if (bin->is_sub() && bin->get_operand(0) == iv) {
        auto *c = dynamic_cast<ConstantInt *>(bin->get_operand(1));
        if (!c || c->value_ == INT_MIN) return false;
        offset = -c->value_;
        return true;
    }
    return false;
}

bool varyingIndexHasUnitElementStride(GetElementPtrInst *gep, unsigned varyingIndex,
                                      Type *scalarType) {
    if (!gep || gep->get_operand(0)->type_->tid_ != Type::PointerTyID)
        return false;
    Type *current =
        static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int indexBytes = typeSize(current);
        if (i == varyingIndex)
            return indexBytes == scalarBytes;
        if (current->tid_ == Type::ArrayTyID)
            current = static_cast<ArrayType *>(current)->contained_;
        else if (current->tid_ == Type::PointerTyID)
            current = static_cast<PointerType *>(current)->contained_;
    }
    return false;
}

bool pointerOffsetFromPhi(Value *pointer, PhiInst *phi, int &offset) {
    offset = 0;
    Value *cursor = pointer;
    while (cursor != phi) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(cursor);
        if (!gep || gep->num_ops_ != 2) return false;
        auto *constant = dynamic_cast<ConstantInt *>(gep->get_operand(1));
        if (!constant) return false;
        offset += constant->value_;
        cursor = gep->get_operand(0);
    }
    return true;
}

bool isMemsetFillConstant(ConstantInt *ci, int &fillByte) {
    int v = ci->value_;
    fillByte = v & 0xFF;
    for (int shift = 8; shift < 32; shift += 8) {
        if (((v >> shift) & 0xFF) != fillByte)
            return false;
    }
    return true;
}

struct MemsetMatch {
    StoreInst *store = nullptr;
    Value *base = nullptr;
    GetElementPtrInst *inductionGeep = nullptr;
    unsigned inductionIndex = 0;
    Value *elementCount = nullptr;
    int fillByte = 0;
    int elementStrideBytes = 0;
};

bool hasNestedChildLoop(const Loop &loop, LoopInfo &LI) {
    for (auto &other : LI.allLoops()) {
        if (other.get() == &loop) continue;
        if (other->depth > loop.depth && loop.blocks.count(other->header))
            return true;
    }
    return false;
}

struct IVDesc {
    PhiInst *phi = nullptr;
    AllocaInst *alloca = nullptr;
    Value *bound = nullptr;
};

bool classifyStoreAddressPhi(const Loop &loop, PhiInst *ivPhi, StoreInst *store,
                             MemsetMatch &match) {
    Value *pointer = store->get_operand(1);
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == ivPhi || phi->type_->tid_ != Type::PointerTyID) continue;

        int offset = 0;
        if (!pointerOffsetFromPhi(pointer, phi, offset)) continue;
        if (offset != 0) return false;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            return false;
        auto *update = dynamic_cast<GetElementPtrInst *>(latchValue);
        if (!update || update->num_ops_ != 2 || update->get_operand(0) != phi)
            return false;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (!step || step->value_ != 1) return false;

        match.base = init;
        match.elementStrideBytes = scalarBytes;
        return true;
    }

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned varying = 0;
    int ivOffset = 0;
    int varyingCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        int offset = 0;
        if (matchIVPlusConstant(gep->get_operand(i), ivPhi, offset)) {
            varying = i;
            ivOffset = offset;
            ++varyingCount;
        } else if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), loop.blocks)) return false;
    if (varyingCount != 1 || ivOffset != 0) return false;
    if (!varyingIndexHasUnitElementStride(gep, varying, scalarType)) return false;

    match.inductionGeep = gep;
    match.inductionIndex = varying;
    match.elementStrideBytes = scalarBytes;
    return true;
}

bool classifyStoreAddressAlloca(const Loop &loop, AllocaInst *ivAlloca,
                                StoreInst *store, MemsetMatch &match) {
    Value *pointer = store->get_operand(1);
    Type *scalarType = store->get_operand(0)->type_;
    int scalarBytes = typeSize(scalarType);
    if (scalarBytes <= 0) return false;

    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    unsigned varying = 0;
    int varyingCount = 0;
    for (unsigned i = 1; i < gep->num_ops_; ++i) {
        auto *load = dynamic_cast<LoadInst *>(gep->get_operand(i));
        if (load && load->get_operand(0) == ivAlloca) {
            varying = i;
            ++varyingCount;
        } else if (!isLoopInvariant(gep->get_operand(i), loop.blocks)) {
            return false;
        }
    }
    if (!isLoopInvariant(gep->get_operand(0), loop.blocks)) return false;
    if (varyingCount != 1) return false;
    if (!varyingIndexHasUnitElementStride(gep, varying, scalarType)) return false;

    match.inductionGeep = gep;
    match.inductionIndex = varying;
    match.elementStrideBytes = scalarBytes;
    return true;
}

bool ivValueMatches(Value *value, const IVDesc &iv) {
    if (iv.phi && value == iv.phi) return true;
    if (iv.alloca) {
        auto *load = dynamic_cast<LoadInst *>(value);
        return load && load->get_operand(0) == iv.alloca;
    }
    return false;
}

bool findCountingIV(Loop &loop, BasicBlock *latch, ICmpInst *compare, IVDesc &iv) {
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    Value *bound = compare->get_operand(1);
    if (!isLoopInvariant(bound, loop.blocks)) return false;

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID ||
            compare->get_operand(0) != phi)
            continue;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            continue;
        auto *initCI = dynamic_cast<ConstantInt *>(init);
        if (!initCI || initCI->value_ != 0) continue;

        auto *update = dynamic_cast<BinaryInst *>(latchValue);
        if (!update || !update->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) != phi || !step || step->value_ != 1)
            continue;

        iv.phi = phi;
        iv.bound = bound;
        return true;
    }

    auto *load = dynamic_cast<LoadInst *>(compare->get_operand(0));
    if (!load) return false;
    auto *alloca = dynamic_cast<AllocaInst *>(load->get_operand(0));
    if (!alloca || alloca->alloca_ty_->tid_ != Type::IntegerTyID) return false;

    bool initZero = false;
    for (auto *inst : loop.preheader->instr_list_) {
        if (!inst->is_store() || inst->get_operand(1) != alloca) continue;
        auto *ci = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (ci && ci->value_ == 0) initZero = true;
    }
    if (!initZero) return false;

    bool foundUpdate = false;
    for (auto *inst : latch->instr_list_) {
        if (!inst->is_store() || inst->get_operand(1) != alloca) continue;
        auto *add = dynamic_cast<BinaryInst *>(inst->get_operand(0));
        if (!add || !add->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(add->get_operand(1));
        if (!step || step->value_ != 1) {
            step = dynamic_cast<ConstantInt *>(add->get_operand(0));
            if (!step || step->value_ != 1) continue;
            if (!ivValueMatches(add->get_operand(1), IVDesc{nullptr, alloca, nullptr}))
                continue;
        } else if (!ivValueMatches(add->get_operand(0), IVDesc{nullptr, alloca, nullptr})) {
            continue;
        }
        foundUpdate = true;
        break;
    }
    if (!foundUpdate) return false;

    iv.alloca = alloca;
    iv.bound = bound;
    return true;
}

bool isAllowedLoopInst(Instruction *inst, const IVDesc &iv, bool &sawStore) {
    if (inst->is_phi() || inst->isTerminator()) return true;
    if (inst->is_call() || inst->is_alloca()) return false;
    if (inst->is_store()) {
        if (sawStore) return false;
        sawStore = true;
        return true;
    }
    if (inst->is_load()) {
        if (!iv.alloca) return false;
        auto *load = static_cast<LoadInst *>(inst);
        return load->get_operand(0) == iv.alloca;
    }
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst);
}

bool classifyStoreAddress(const Loop &loop, const IVDesc &iv, StoreInst *store,
                          MemsetMatch &match) {
    if (iv.phi) return classifyStoreAddressPhi(loop, iv.phi, store, match);
    if (iv.alloca) return classifyStoreAddressAlloca(loop, iv.alloca, store, match);
    return false;
}

bool tryMatchMemsetLoop(Loop &loop, MemsetMatch &match) {
    if (!loop.preheader || !loop.singleLatch() || !loop.singleExit())
        return false;
    if (loop.singleLatch() == loop.header) return false;
    if (loop.blocks.size() < 2 || loop.blocks.size() > 3) return false;

    auto *preTerm = loop.preheader->get_terminator();
    auto *headerTerm = loop.header->get_terminator();
    auto *latch = loop.singleLatch();
    auto *latchTerm = latch->get_terminator();
    auto *exit = loop.singleExit();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != loop.header)
        return false;
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(2) != exit)
        return false;
    if (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
        latchTerm->get_operand(0) != loop.header)
        return false;

    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    if (!bodyEntry || !loop.blocks.count(bodyEntry)) return false;
    if (bodyEntry != latch) {
        if (loop.blocks.size() != 3) return false;
        auto *bodyTerm = bodyEntry->get_terminator();
        if (!bodyTerm || !bodyTerm->is_br() || bodyTerm->num_ops_ != 1 ||
            bodyTerm->get_operand(0) != latch)
            return false;
    }

    auto *compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    IVDesc iv;
    if (!findCountingIV(loop, latch, compare, iv)) return false;

    std::vector<BasicBlock *> recipeBlocks{loop.header, bodyEntry};
    if (latch != bodyEntry) recipeBlocks.push_back(latch);

    StoreInst *store = nullptr;
    bool sawStore = false;
    for (auto *bb : recipeBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (!isAllowedLoopInst(inst, iv, sawStore)) return false;
            if (inst->is_store()) store = static_cast<StoreInst *>(inst);
        }
    }
    if (!store || !sawStore) return false;

    auto *fillCI = dynamic_cast<ConstantInt *>(store->get_operand(0));
    if (!fillCI) return false;
    int fillByte = 0;
    if (!isMemsetFillConstant(fillCI, fillByte)) return false;

    MemsetMatch local;
    if (!classifyStoreAddress(loop, iv, store, local)) return false;

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == loop.header) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    match.store = store;
    match.base = local.base;
    match.inductionGeep = local.inductionGeep;
    match.inductionIndex = local.inductionIndex;
    match.elementCount = iv.bound;
    match.fillByte = fillByte;
    match.elementStrideBytes = local.elementStrideBytes;
    return true;
}

Function *getOrCreateMemsetDecl(Module *module, Function **cache) {
    if (*cache) return *cache;
    for (auto *func : module->function_list_) {
        if (func->name_ == "memset") {
            *cache = func;
            return func;
        }
    }
    auto *ptrTy = module->get_pointer_type(module->int32_ty_);
    auto *fty = new FunctionType(
        module->void_ty_, {ptrTy, module->int32_ty_, module->int32_ty_});
    *cache = new Function(fty, "memset", module);
    return *cache;
}

bool lowerMemsetLoop(Loop &loop, const MemsetMatch &match, Module *module,
                     Function *memsetDecl, Function *func) {
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !exit) return false;

    auto *preTerm = preheader->get_terminator();
    if (!preTerm || !preTerm->is_br()) return false;
    int headerOperand = -1;
    for (unsigned i = 0; i < preTerm->num_ops_; i++) {
        if (preTerm->get_operand(i) == loop.header) {
            headerOperand = static_cast<int>(i);
            break;
        }
    }
    if (headerOperand < 0) return false;

    int stride = match.elementStrideBytes;
    Value *byteCount = nullptr;
    if (stride == 1) {
        byteCount = match.elementCount;
    } else {
        auto *strideC = new ConstantInt(module->int32_ty_, stride);
        auto *mul = new BinaryInst(
            module->int32_ty_, Instruction::Mul, match.elementCount, strideC,
            preheader, true);
        preheader->add_instruction_before_terminator(mul);
        byteCount = mul;
    }

    auto *boundCI = dynamic_cast<ConstantInt *>(match.elementCount);
    if (boundCI) {
        long long bytes = 0;
        if (!RecurrenceAnalysis::checkedMul(boundCI->value_, stride, bytes) ||
            bytes > INT_MAX)
            return false;
    }

    Value *base = match.base;
    if (!base && match.inductionGeep) {
        std::vector<Value *> idxs;
        Module *mod = func->parent_;
        for (unsigned i = 1; i < match.inductionGeep->num_ops_; ++i) {
            if (i == match.inductionIndex)
                idxs.push_back(new ConstantInt(mod->int32_ty_, 0));
            else
                idxs.push_back(match.inductionGeep->get_operand(i));
        }
        auto *firstGEP = GetElementPtrInst::create_split_suffix_gep(
            match.inductionGeep->get_operand(0), idxs, preheader, true);
        preheader->add_instruction_before_terminator(firstGEP);
        base = firstGEP;
    }
    if (!base) return false;
    auto *ptrTy = module->get_pointer_type(module->int32_ty_);
    if (base->type_ != ptrTy) {
        auto *cast = new Bitcast(Instruction::BitCast, base, ptrTy, preheader, true);
        preheader->add_instruction_before_terminator(cast);
        base = cast;
    }

    auto *fillC = new ConstantInt(module->int32_ty_, match.fillByte);
    std::vector<Value *> args{base, fillC, byteCount};
    auto *call = new CallInst(memsetDecl, args, preheader, true);
    preheader->add_instruction_before_terminator(call);
    preheader->setSemFlag(SemFlag::MemsetIdiomLoop);

    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        std::vector<unsigned> loopIncoming;
        Value *fromLoop = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred && loop.blocks.count(pred)) {
                loopIncoming.push_back(i);
                fromLoop = phi->get_operand(i);
            }
        }
        if (loopIncoming.empty()) continue;
        if (!fromLoop || !isLoopInvariant(fromLoop, loop.blocks)) return false;
        for (unsigned idx : loopIncoming)
            phi->remove_operands(static_cast<int>(idx),
                                 static_cast<int>(idx + 1));
    }

    preTerm->set_operand(static_cast<unsigned>(headerOperand), exit);
    preheader->remove_succ_basic_block(loop.header);
    preheader->add_succ_basic_block(exit);
    loop.header->remove_pre_basic_block(preheader);
    exit->add_pre_basic_block(preheader);

    std::vector<BasicBlock *> deadBlocks(loop.blocksOrdered.begin(),
                                         loop.blocksOrdered.end());
    for (auto *bb : deadBlocks)
        func->remove_bb(bb);
    return true;
}

} // namespace

void IdiomRecognize::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses IdiomRecognize::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, AM);
    }
    memsetDecl_ = nullptr;
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool IdiomRecognize::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;
        LoopInfo &LI = AM.getLoopInfo(func);

        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });

        for (auto *loop : loops) {
            if (hasNestedChildLoop(*loop, LI)) continue;

            MemsetMatch match;
            if (!tryMatchMemsetLoop(*loop, match)) continue;

            Function *memsetDecl =
                getOrCreateMemsetDecl(func->parent_, &memsetDecl_);
            if (!lowerMemsetLoop(*loop, match, func->parent_, memsetDecl, func))
                continue;

            changed = true;
            progress = true;
            func->set_instr_name();
            AM.invalidateFunction(func, PreservedAnalyses::none());
            break;
        }
    }
    return changed;
}
