#include "../../../include/mid/opt/loopDistribution.hpp"
#include "../../../include/mid/opt/cfgUtils.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <set>
#include <unordered_map>
#include <vector>

namespace {

bool isScratchAlloca(AllocaInst *alloca, int size) {
    if (!alloca || !alloca->isLoopExpansionScratch()) return false;
    auto *arr = dynamic_cast<ArrayType *>(alloca->alloca_ty_);
    return arr && static_cast<int>(arr->num_elements_) == size;
}

AllocaInst *findUnusedScratch(Function *func, int size,
                              const std::set<AllocaInst *> &reserved) {
    if (!func || func->basic_blocks_.empty()) return nullptr;
    for (auto *inst : func->basic_blocks_.front()->instr_list_) {
        auto *alloca = dynamic_cast<AllocaInst *>(inst);
        if (!isScratchAlloca(alloca, size)) continue;
        if (reserved.count(alloca)) continue;
        if (!alloca->use_list_.empty()) continue;
        return alloca;
    }
    return nullptr;
}

AllocaInst *createScratch(Function *func, int size, int &counter) {
    Module *module = func->parent_;
    auto *arr = module->get_array_type(module->int32_ty_, size);
    auto *entry = func->basic_blocks_.front();
    auto *alloca = new AllocaInst(arr, entry, true);
    alloca->markLoopExpansionScratch();
    alloca->name_ = "scalar.expansion.tmp." + std::to_string(counter++);
    entry->add_instruction_front(alloca);
    return alloca;
}

Instruction *firstNonPhi(BasicBlock *bb) {
    for (auto *inst : bb->instr_list_) {
        if (!inst->is_phi()) return inst;
    }
    return nullptr;
}

BasicBlock *loopBodyEntry(Loop *loop) {
    if (!loop || !loop->header) return nullptr;
    auto *br = dynamic_cast<BranchInst *>(loop->header->get_terminator());
    if (!br || br->num_ops_ != 3) return nullptr;
    auto *t = dynamic_cast<BasicBlock *>(br->get_operand(1));
    auto *f = dynamic_cast<BasicBlock *>(br->get_operand(2));
    if (t && loop->blocks.count(t)) return t;
    if (f && loop->blocks.count(f)) return f;
    return nullptr;
}

void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldT, BasicBlock *newT) {
    auto *term = pred ? pred->get_terminator() : nullptr;
    if (!term || !term->is_br()) return;
    bool changed = false;
    for (unsigned i = 0; i < term->num_ops_; i++) {
        if (term->get_operand(i) == oldT) {
            term->set_operand(i, newT);
            changed = true;
        }
    }
    if (!changed) return;
    pred->remove_succ_basic_block(oldT);
    oldT->remove_pre_basic_block(pred);
    pred->add_succ_basic_block(newT);
    newT->add_pre_basic_block(pred);
}

void retargetPhiPred(BasicBlock *succ, BasicBlock *oldPred, BasicBlock *newPred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        for (unsigned i = 0; i + 1 < inst->num_ops_; i += 2) {
            if (inst->get_operand(i + 1) == oldPred)
                inst->set_operand(i + 1, newPred);
        }
    }
}

GetElementPtrInst *insertScratchGEP(AllocaInst *scratch, Value *zero,
                                    Value *index, BasicBlock *bb,
                                    Instruction *before) {
    auto *gep = new GetElementPtrInst(scratch, {zero, index}, bb, true);
    bb->add_instruction_before_inst(gep, before);
    return gep;
}

LoadInst *insertScratchLoad(AllocaInst *scratch, Value *zero, Value *index,
                            BasicBlock *bb, Instruction *before) {
    auto *gep = insertScratchGEP(scratch, zero, index, bb, before);
    auto *load = new LoadInst(gep, bb, true);
    bb->add_instruction_before_inst(load, before);
    return load;
}

void insertScratchStore(AllocaInst *scratch, Value *zero, Value *index,
                        Value *stored, BasicBlock *bb) {
    auto *term = bb->get_terminator();
    auto *gep = new GetElementPtrInst(scratch, {zero, index}, bb, true);
    bb->add_instruction_before_terminator(gep);
    auto *store = new StoreInst(stored, gep, bb, true);
    bb->add_instruction_before_terminator(store);
}

struct DistributedReduction {
    ScalarReductionInfo reduction;
    AllocaInst *scratch = nullptr;
};

struct CountedLoopBlocks {
    BasicBlock *header = nullptr;
    BasicBlock *body = nullptr;
    BasicBlock *latch = nullptr;
    PhiInst *iv = nullptr;
};

BasicBlock *newBlock(Module *module, Function *func, const std::string &tag,
                     int &counter) {
    return new BasicBlock(module, tag + "." + std::to_string(counter++), func);
}

CountedLoopBlocks createCountedLoop(Module *module, Function *func,
                                    const std::string &prefix, Value *bound,
                                    BasicBlock *predecessor, BasicBlock *exit,
                                    int &counter) {
    CountedLoopBlocks loop;
    loop.header = newBlock(module, func, prefix + ".h", counter);
    loop.body = newBlock(module, func, prefix + ".body", counter);
    loop.latch = newBlock(module, func, prefix + ".latch", counter);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);

    loop.iv = PhiInst::create_phi(module->int32_ty_, loop.header);
    loop.iv->add_phi_pair_operand(zero, predecessor);
    loop.header->add_instruction_front(loop.iv);
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, loop.iv, bound, loop.header);
    new BranchInst(cmp, loop.body, exit, loop.header);

    new BranchInst(loop.latch, loop.body);
    auto *inc = new BinaryInst(module->int32_ty_, Instruction::Add,
                               loop.iv, one, loop.latch);
    loop.iv->add_phi_pair_operand(inc, loop.latch);
    new BranchInst(loop.header, loop.latch);
    return loop;
}

void replaceUsesInLoop(PhiInst *oldPhi, Value *replacement, StoreInst *deadStore,
                       Loop *parentLoop) {
    auto uses = oldPhi->use_list_;
    for (const auto &use : uses) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || user == oldPhi || user == deadStore) continue;
        if (!user->parent_ || !parentLoop->blocks.count(user->parent_)) continue;
        user->set_operand(use.arg_no_, replacement);
    }
}

using ValueMap = std::unordered_map<Value *, Value *>;

Value *remapValue(Value *value, const ValueMap &map) {
    auto found = map.find(value);
    return found == map.end() ? value : found->second;
}

bool isCloneableBodyInstruction(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<SelectInst *>(inst);
}

Instruction *cloneBodyInstruction(Instruction *orig, BasicBlock *dest,
                                  const ValueMap &map) {
    auto R = [&](Value *value) { return remapValue(value, map); };
    Instruction *clone = nullptr;
    if (auto *binary = dynamic_cast<BinaryInst *>(orig)) {
        clone = new BinaryInst(binary->type_, binary->op_id_,
                               R(binary->get_operand(0)),
                               R(binary->get_operand(1)), dest);
    } else if (auto *unary = dynamic_cast<UnaryInst *>(orig)) {
        clone = new UnaryInst(unary->type_, unary->op_id_,
                              R(unary->get_operand(0)), dest);
    } else if (auto *cmp = dynamic_cast<ICmpInst *>(orig)) {
        clone = new ICmpInst(cmp->icmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest);
    } else if (auto *cmp = dynamic_cast<FCmpInst *>(orig)) {
        clone = new FCmpInst(cmp->fcmp_op_, R(cmp->get_operand(0)),
                             R(cmp->get_operand(1)), dest);
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops_; ++i)
            indices.push_back(R(gep->get_operand(i)));
        clone = new GetElementPtrInst(R(gep->get_operand(0)), indices, dest);
    } else if (auto *load = dynamic_cast<LoadInst *>(orig)) {
        clone = new LoadInst(R(load->get_operand(0)), dest);
    } else if (auto *zext = dynamic_cast<ZextInst *>(orig)) {
        clone = new ZextInst(zext->op_id_, R(zext->get_operand(0)),
                             zext->dest_ty_, dest);
    } else if (auto *cast = dynamic_cast<FpToSiInst *>(orig)) {
        clone = new FpToSiInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->dest_ty_, dest);
    } else if (auto *cast = dynamic_cast<SiToFpInst *>(orig)) {
        clone = new SiToFpInst(cast->op_id_, R(cast->get_operand(0)),
                               cast->dest_ty_, dest);
    } else if (auto *cast = dynamic_cast<Bitcast *>(orig)) {
        clone = new Bitcast(cast->op_id_, R(cast->get_operand(0)),
                            cast->dest_ty_, dest);
    } else if (auto *select = dynamic_cast<SelectInst *>(orig)) {
        clone = new SelectInst(R(select->get_operand(0)),
                               R(select->get_operand(1)),
                               R(select->get_operand(2)), dest);
    }
    if (clone) clone->copySemFlagsFrom(orig);
    return clone;
}

Value *clonePureValue(Value *value, BasicBlock *dest, ValueMap &map,
                      std::set<Value *> &visiting, Loop *scope) {
    if (!value) return nullptr;
    auto mapped = map.find(value);
    if (mapped != map.end()) return mapped->second;
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return value;

    auto *inst = dynamic_cast<Instruction *>(value);
    if (inst && scope && !scope->blocks.count(inst->parent_)) return value;
    if (!inst || !isCloneableBodyInstruction(inst) ||
        !visiting.insert(value).second)
        return nullptr;

    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        if (dynamic_cast<BasicBlock *>(inst->get_operand(i))) continue;
        if (!clonePureValue(inst->get_operand(i), dest, map, visiting, scope))
            return nullptr;
    }
    auto *clone = cloneBodyInstruction(inst, dest, map);
    if (!clone) return nullptr;
    map[value] = clone;
    visiting.erase(value);
    return clone;
}

} // namespace

void LoopDistribution::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

void LoopDistribution::runOnFunction(Function *func) {
    for (int iter = 0; iter < 32; iter++) {
        LoopInfo LI;
        LI.analyze(func);
        if (LI.allLoops().empty()) return;

        AffineAnalysis     AA(LI);
        DependenceAnalysis DA(LI, AA);
        CostModel          CM(AA);
        ReductionAnalysis  RA(AA);
        LoopAccessAnalysis LA(AA);
        LoopInterchangeAnalysis IA(DA, LA, CM);

        bool changed = false;
        for (auto &L_ptr : LI.allLoops()) {
            Loop *L = L_ptr.get();
            if (!L->children.empty()) continue;
            ScalarReductionNestInfo info{};
            if (!RA.detectScalarExpandableNest(L, info)) continue;
            if (!RA.isScalarExpansionMemoryLegal(info)) continue;
            if (!isLegalAndProfitable(info, IA)) continue;
            if (apply(info, func->parent_)) {
                changed = true;
                break;
            }
        }
        if (!changed) return;
    }
}

bool LoopDistribution::isLegalAndProfitable(const ScalarReductionNestInfo &info,
                                            LoopInterchangeAnalysis &IA) {
    PhiInst *L_iv = info.inner_loop->getInductionIV();
    PhiInst *P_iv = info.parent_loop->getInductionIV();
    std::vector<GetElementPtrInst *> geps = info.body_geps;
    for (auto &r : info.reductions) geps.push_back(r.gep_store);
    return IA.estimateCost(geps, L_iv, P_iv).profitable();
}

bool LoopDistribution::apply(const ScalarReductionNestInfo &info, Module *module) {
    Loop *P = info.parent_loop;
    Loop *L = info.inner_loop;
    Function *func = P->header->parent_;
    Type *i32 = module->int32_ty_;
    BasicBlock *P_preheader = P->preheader;
    BasicBlock *P_exit = P->singleExit();
    BasicBlock *L_header = L->header;
    BasicBlock *L_latch = L->singleLatch();
    PhiInst *P_iv = P->getInductionIV();
    PhiInst *L_iv = L->getInductionIV();
    if (!P_preheader || !P_exit || !L_header || !L_latch ||
        !P_iv || !L_iv || !info.parent_bound || !info.inner_bound)
        return false;

    auto *preTerm = P_preheader->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != P->header)
        return false;

    BasicBlock *bodyEntry = loopBodyEntry(L);
    if (!bodyEntry) return false;

    std::vector<BasicBlock *> originalBlocks;
    for (auto *bb : L->blocksOrdered) {
        if (bb != L_header) originalBlocks.push_back(bb);
    }
    if (originalBlocks.empty()) return false;

    // Validate the complete clone before changing CFG or allocating scratch;
    // a failed transform must leave no partial distribution behind.
    for (auto *bb : originalBlocks) {
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!term) return false;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            if (!isCloneableBodyInstruction(inst)) return false;
        }
    }

    std::vector<BasicBlock *> oldExitPreds;
    for (auto *pred : P_exit->pre_bbs_)
        if (P->blocks.count(pred)) oldExitPreds.push_back(pred);
    if (oldExitPreds.size() != 1) return false;
    BasicBlock *oldExitPred = oldExitPreds.front();

    std::set<AllocaInst *> reserved;
    std::vector<DistributedReduction> reductions;
    for (const auto &reduction : info.reductions) {
        AllocaInst *scratch = findUnusedScratch(func, reduction.inner_dim,
                                                reserved);
        if (!scratch)
            scratch = createScratch(func, reduction.inner_dim,
                                    scratch_counter_);
        reserved.insert(scratch);
        reductions.push_back({reduction, scratch});
    }

    auto block = [&](const std::string &name) {
        return newBlock(module, func, name, block_counter_);
    };
    BasicBlock *clearHeader = block("ldist.clear.h");
    BasicBlock *clearBody = block("ldist.clear.body");
    BasicBlock *clearLatch = block("ldist.clear.latch");
    BasicBlock *outerHeader = block("ldist.interchanged.outer.h");
    BasicBlock *outerBody = block("ldist.interchanged.outer.body");
    BasicBlock *outerLatch = block("ldist.interchanged.outer.latch");
    BasicBlock *innerHeader = block("ldist.interchanged.inner.h");
    BasicBlock *innerLatch = block("ldist.interchanged.inner.latch");
    BasicBlock *storeHeader = block("ldist.storeback.h");
    BasicBlock *storeBody = block("ldist.storeback.body");
    BasicBlock *storeLatch = block("ldist.storeback.latch");

    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);
    Value *parentInit = P->inductionInit;
    if (!parentInit) return false;

    auto *clearIV = PhiInst::create_phi(i32, clearHeader);
    clearIV->add_phi_pair_operand(zero, P_preheader);
    clearHeader->add_instruction_front(clearIV);
    auto *clearCmp = new ICmpInst(ICmpInst::ICMP_SLT, clearIV,
                                  info.parent_bound, clearHeader);
    new BranchInst(clearCmp, clearBody, outerHeader, clearHeader);
    std::vector<std::pair<AllocaInst *, Value *>> initialValues;
    for (const auto &entry : reductions) {
        ValueMap initMap;
        initMap[P_iv] = clearIV;
        std::set<Value *> visiting;
        Value *initialValue = clonePureValue(entry.reduction.sum_init,
                                             clearBody, initMap, visiting, P);
        if (!initialValue) return false;
        initialValues.push_back({entry.scratch, initialValue});
    }
    new BranchInst(clearLatch, clearBody);
    for (const auto &[scratch, initialValue] : initialValues) {
        insertScratchStore(scratch, zero, clearIV, initialValue,
                           clearBody);
    }
    auto *clearNext = new BinaryInst(i32, Instruction::Add, clearIV, one,
                                     clearLatch);
    clearIV->add_phi_pair_operand(clearNext, clearLatch);
    new BranchInst(clearHeader, clearLatch);

    // The old inner dimension becomes outer; the old parent dimension becomes
    // inner.  Clone the complete old inner body so conditional merges and
    // arbitrary internal CFG remain in SSA form.
    auto *outerIV = PhiInst::create_phi(i32, outerHeader);
    outerIV->add_phi_pair_operand(zero, clearHeader);
    outerHeader->add_instruction_front(outerIV);
    auto *outerCmp = new ICmpInst(ICmpInst::ICMP_SLT, outerIV,
                                  info.inner_bound, outerHeader);
    new BranchInst(outerCmp, outerBody, storeHeader, outerHeader);
    new BranchInst(innerHeader, outerBody);

    auto *innerIV = PhiInst::create_phi(i32, innerHeader);
    innerIV->add_phi_pair_operand(parentInit, outerBody);
    innerHeader->add_instruction_front(innerIV);

    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto *oldBlock : originalBlocks)
        blockMap[oldBlock] = block("ldist.clone");
    BasicBlock *clonedEntry = blockMap[bodyEntry];

    ValueMap valueMap;
    valueMap[L_iv] = outerIV;
    valueMap[P_iv] = innerIV;
    for (const auto &entry : reductions) {
        auto *gep = new GetElementPtrInst(entry.scratch, {zero, innerIV},
                                          clonedEntry);
        auto *load = new LoadInst(gep, clonedEntry);
        valueMap[entry.reduction.sum_phi] = load;
    }

    std::set<PhiInst *> reductionPhis;
    for (const auto &entry : reductions)
        reductionPhis.insert(entry.reduction.sum_phi);

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (!inst->is_phi()) break;
            auto *oldPhi = static_cast<PhiInst *>(inst);
            if (reductionPhis.count(oldPhi)) continue;
            auto *newPhi = PhiInst::create_phi(oldPhi->type_, newBB);
            newPhi->copySemFlagsFrom(oldPhi);
            newBB->add_instruction_front(newPhi);
            valueMap[oldPhi] = newPhi;
        }
    }

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            Instruction *clone = cloneBodyInstruction(inst, newBB, valueMap);
            if (!clone) return false;
            valueMap[inst] = clone;
        }
    }

    auto mappedBlock = [&](BasicBlock *oldBlock) -> BasicBlock * {
        if (oldBlock == L_header) return innerLatch;
        auto found = blockMap.find(oldBlock);
        return found == blockMap.end() ? nullptr : found->second;
    };

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        for (auto *inst : oldBlock->instr_list_) {
            if (!inst->is_phi()) break;
            auto *oldPhi = static_cast<PhiInst *>(inst);
            if (reductionPhis.count(oldPhi)) continue;
            auto *newPhi = static_cast<PhiInst *>(valueMap[oldPhi]);
            for (unsigned i = 0; i < oldPhi->num_ops_; i += 2) {
                auto *oldPred = static_cast<BasicBlock *>(
                    oldPhi->get_operand(i + 1));
                if (oldPred == L_header || oldPred == L->preheader) continue;
                BasicBlock *newPred = mappedBlock(oldPred);
                if (!newPred) return false;
                newPhi->add_phi_pair_operand(
                    remapValue(oldPhi->get_operand(i), valueMap), newPred);
            }
        }
    }

    for (auto *oldBlock : originalBlocks) {
        BasicBlock *newBB = blockMap[oldBlock];
        if (oldBlock == L_latch) {
            for (const auto &entry : reductions) {
                auto *gep = new GetElementPtrInst(entry.scratch,
                                                  {zero, innerIV}, newBB);
                new StoreInst(remapValue(entry.reduction.sum_latch, valueMap),
                              gep, newBB);
            }
        }

        auto *oldBranch = static_cast<BranchInst *>(oldBlock->get_terminator());
        if (oldBranch->num_ops_ == 1) {
            BasicBlock *dest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(0)));
            if (!dest) return false;
            new BranchInst(dest, newBB);
        } else {
            BasicBlock *trueDest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(1)));
            BasicBlock *falseDest = mappedBlock(static_cast<BasicBlock *>(
                oldBranch->get_operand(2)));
            if (!trueDest || !falseDest) return false;
            new BranchInst(remapValue(oldBranch->get_operand(0), valueMap),
                           trueDest, falseDest, newBB);
        }
    }

    auto *innerCmp = new ICmpInst(ICmpInst::ICMP_SLT, innerIV,
                                  info.parent_bound, innerHeader);
    new BranchInst(innerCmp, clonedEntry, outerLatch, innerHeader);
    auto *innerNext = new BinaryInst(i32, Instruction::Add, innerIV, one,
                                     innerLatch);
    innerIV->add_phi_pair_operand(innerNext, innerLatch);
    new BranchInst(innerHeader, innerLatch);
    auto *outerNext = new BinaryInst(i32, Instruction::Add, outerIV, one,
                                     outerLatch);
    outerIV->add_phi_pair_operand(outerNext, outerLatch);
    new BranchInst(outerHeader, outerLatch);

    auto *storeIV = PhiInst::create_phi(i32, storeHeader);
    storeIV->add_phi_pair_operand(parentInit, outerHeader);
    storeHeader->add_instruction_front(storeIV);
    auto *storeCmp = new ICmpInst(ICmpInst::ICMP_SLT, storeIV,
                                  info.parent_bound, storeHeader);
    new BranchInst(storeCmp, storeBody, P_exit, storeHeader);
    for (const auto &entry : reductions) {
        auto *loadGEP = new GetElementPtrInst(entry.scratch,
                                              {zero, storeIV}, storeBody);
        auto *value = new LoadInst(loadGEP, storeBody);
        std::vector<Value *> destinationIndices;
        auto *originalGEP = entry.reduction.gep_store;
        unsigned last = originalGEP->num_ops_ - 1;
        for (unsigned i = 1; i < originalGEP->num_ops_; ++i)
            destinationIndices.push_back(
                i == last ? static_cast<Value *>(storeIV)
                          : originalGEP->get_operand(i));
        auto *destination = new GetElementPtrInst(
            entry.reduction.base_store, destinationIndices, storeBody);
        new StoreInst(value, destination, storeBody);
    }
    new BranchInst(storeLatch, storeBody);
    auto *storeNext = new BinaryInst(i32, Instruction::Add, storeIV, one,
                                     storeLatch);
    storeIV->add_phi_pair_operand(storeNext, storeLatch);
    new BranchInst(storeHeader, storeLatch);

    // Commit only after the replacement nest is complete.
    preTerm->set_operand(0, clearHeader);
    P_preheader->remove_succ_basic_block(P->header);
    P->header->remove_pre_basic_block(P_preheader);
    P_preheader->add_succ_basic_block(clearHeader);
    clearHeader->add_pre_basic_block(P_preheader);
    retargetPhiPred(P_exit, oldExitPred, storeHeader);
    removeUnreachableBlocks(func);
    return true;
}
