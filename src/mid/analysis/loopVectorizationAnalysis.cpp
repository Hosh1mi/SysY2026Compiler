#include "../../include/mid/analysis/loopVectorizationAnalysis.hpp"
#include "../../include/mid/analysis/vectorizationCostModel.hpp"
#include "../../include/mid/ir/intrinsics.hpp"

#include <algorithm>
#include <climits>
#include <set>

namespace {

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

bool varyingIndexHasUnitElementStride(GetElementPtrInst *gep,
                                      unsigned varyingIndex,
                                      Type *scalarType) {
    if (!gep || gep->get_operand(0)->type_->tid_ != Type::PointerTyID)
        return false;
    Type *current = static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
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

Value *memoryPointer(Instruction *inst) {
    return inst->is_load() ? inst->get_operand(0) : inst->get_operand(1);
}

bool sameAddressShape(const LoopVectorizationAnalysis::MemoryAccess &a,
                      const LoopVectorizationAnalysis::MemoryAccess &b) {
    if (a.addressKind != b.addressKind) return false;
    if (a.addressKind == LoopVectorizationAnalysis::AddressKind::Uniform)
        return memoryPointer(a.inst) == memoryPointer(b.inst);
    if (a.addressKind == LoopVectorizationAnalysis::AddressKind::PointerRecurrence)
        return a.pointerPhi == b.pointerPhi &&
               a.pointerOffset == b.pointerOffset;

    if (!a.gep || !b.gep || a.gep->num_ops_ != b.gep->num_ops_ ||
        a.varyingIndex != b.varyingIndex || a.ivOffset != b.ivOffset)
        return false;
    for (unsigned i = 0; i < a.gep->num_ops_; ++i) {
        if (i == a.varyingIndex) continue;
        if (a.gep->get_operand(i) != b.gep->get_operand(i)) return false;
    }
    return true;
}

} // namespace

bool LoopVectorizationAnalysis::reject(std::string *reason,
                                       const char *message) const {
    if (reason) *reason = message;
    return false;
}

bool LoopVectorizationAnalysis::isLoopInvariant(Value *value,
                                                const Loop &loop) const {
    if (dynamic_cast<Constant *>(value) || dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && !loop.blocks.count(inst->parent_);
}

bool LoopVectorizationAnalysis::findInduction(Loop &loop, Plan &plan,
                                              std::string *reason) const {
    auto *headerTerm = loop.header->get_terminator();
    if (!headerTerm || !headerTerm->is_br() || headerTerm->num_ops_ != 3)
        return reject(reason, "header is not a conditional branch");
    auto *compare = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT)
        return reject(reason, "loop condition is not canonical signed-less-than");

    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            continue;
        auto *update = dynamic_cast<BinaryInst *>(latchValue);
        if (!update || !update->is_add()) continue;
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (update->get_operand(0) != phi || !step || step->value_ != 1)
            continue;

        bool comparesCurrent = compare->get_operand(0) == phi;
        bool comparesNext = plan.rotatedSingleBlock &&
                            compare->get_operand(0) == update;
        if (!comparesCurrent && !comparesNext) continue;

        Value *bound = compare->get_operand(1);
        if (!isLoopInvariant(bound, loop))
            return reject(reason, "loop bound is not invariant");
        plan.induction = {phi, init, update, bound, compare, 1};
        return true;
    }
    return reject(reason, "canonical unit-stride induction was not found");
}

bool LoopVectorizationAnalysis::findPointerRecurrences(
    Plan &plan, std::string *reason) const {
    Loop &loop = *plan.loop;
    for (auto *inst : plan.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == plan.induction.phi) continue;

        Value *init = nullptr;
        Value *latchValue = nullptr;
        BasicBlock *latchBlock = nullptr;
        if (!getLoopPhiIncoming(loop, phi, init, latchValue, latchBlock))
            return reject(reason, "header phi does not have canonical incoming edges");
        if (phi->type_->tid_ != Type::PointerTyID)
            return reject(reason, "non-induction scalar recurrence is unsupported");

        auto *update = dynamic_cast<GetElementPtrInst *>(latchValue);
        if (!update || update->num_ops_ != 2 || update->get_operand(0) != phi)
            return reject(reason, "pointer recurrence is not a constant-step GEP");
        auto *step = dynamic_cast<ConstantInt *>(update->get_operand(1));
        if (!step || step->value_ != 1)
            return reject(reason, "pointer recurrence is not unit stride");
        plan.pointerRecurrences.push_back({phi, init, update, 1});
    }
    return true;
}

bool LoopVectorizationAnalysis::classifyMemory(Plan &plan,
                                               std::string *reason) const {
    auto recordAccess = [&](MemoryAccess access) {
        access.addressGroup = plan.memoryAccesses.size();
        if (access.addressKind != AddressKind::Uniform) {
            for (const auto &previous : plan.memoryAccesses) {
                if (previous.addressKind == AddressKind::Uniform ||
                    previous.scalarType != access.scalarType ||
                    previous.underlyingObject != access.underlyingObject ||
                    !sameAddressShape(previous, access))
                    continue;
                access.addressGroup = previous.addressGroup;
                break;
            }
        }
        plan.accessForInst[access.inst] = plan.memoryAccesses.size();
        plan.memoryAccesses.push_back(access);
    };

    int order = 0;
    for (auto *inst : plan.recipes) {
        if (!inst->is_load() && !inst->is_store()) {
            ++order;
            continue;
        }

        MemoryAccess access;
        access.kind = inst->is_load() ? AccessKind::Load : AccessKind::Store;
        access.inst = inst;
        access.scalarType = inst->is_load() ? inst->type_ : inst->get_operand(0)->type_;
        access.programOrder = order++;
        if (access.scalarType->tid_ != Type::IntegerTyID &&
            access.scalarType->tid_ != Type::FloatTyID)
            return reject(reason, "memory element is not i32 or float");

        Value *pointer = memoryPointer(inst);
        bool classified = false;
        for (const auto &recurrence : plan.pointerRecurrences) {
            int offset = 0;
            if (!pointerOffsetFromPhi(pointer, recurrence.phi, offset)) continue;
            access.addressKind = AddressKind::PointerRecurrence;
            access.pointerPhi = recurrence.phi;
            access.pointerOffset = offset;
            access.underlyingObject = BAA_.getUnderlyingObject(recurrence.init);
            classified = true;
            break;
        }

        if (!classified) {
            auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
            if (!gep) {
                if (access.kind == AccessKind::Load &&
                    isLoopInvariant(pointer, *plan.loop)) {
                    access.addressKind = AddressKind::Uniform;
                    access.underlyingObject = BAA_.getUnderlyingObject(pointer);
                    classified = true;
                } else {
                    return reject(reason, "memory address is neither affine GEP nor pointer recurrence");
                }
            }

            if (classified) {
                recordAccess(access);
                continue;
            }

            unsigned varying = 0;
            int ivOffset = 0;
            int varyingCount = 0;
            for (unsigned i = 1; i < gep->num_ops_; ++i) {
                int offset = 0;
                if (matchIVPlusConstant(gep->get_operand(i),
                                        plan.induction.phi, offset)) {
                    varying = i;
                    ivOffset = offset;
                    ++varyingCount;
                } else if (!isLoopInvariant(gep->get_operand(i), *plan.loop)) {
                    return reject(reason, "GEP has a non-invariant non-induction index");
                }
            }
            if (!isLoopInvariant(gep->get_operand(0), *plan.loop))
                return reject(reason, "GEP base is loop variant");
            if (varyingCount == 0 && access.kind == AccessKind::Load) {
                access.addressKind = AddressKind::Uniform;
                access.gep = gep;
                access.underlyingObject =
                    BAA_.getUnderlyingObject(gep->get_operand(0));
                recordAccess(access);
                continue;
            }
            if (varyingCount != 1)
                return reject(reason, "GEP does not have exactly one induction index");
            if (!varyingIndexHasUnitElementStride(gep, varying,
                                                  access.scalarType))
                return reject(reason, "GEP induction dimension is not contiguous");

            access.addressKind = AddressKind::InductionGEP;
            access.gep = gep;
            access.varyingIndex = varying;
            access.ivOffset = ivOffset;
            access.underlyingObject = BAA_.getUnderlyingObject(gep->get_operand(0));
        }

        recordAccess(access);
    }
    if (plan.memoryAccesses.empty())
        return reject(reason, "loop has no memory operations");
    return true;
}

bool LoopVectorizationAnalysis::checkInstructions(Plan &plan,
                                                  std::string *reason) const {
    std::set<Instruction *> addressHelpers;
    for (const auto &access : plan.memoryAccesses) {
        Value *cursor = memoryPointer(access.inst);
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(cursor)) {
            addressHelpers.insert(gep);
            cursor = gep->get_operand(0);
        }
    }
    for (const auto &recurrence : plan.pointerRecurrences)
        addressHelpers.insert(recurrence.update);

    bool hasStore = false;
    bool sawStore = false;
    bool storesAreTerminal = true;
    for (auto *inst : plan.recipes) {
        if (inst == plan.induction.update || inst == plan.induction.compare ||
            addressHelpers.count(inst))
            continue;
        if (inst->is_load()) {
            if (sawStore) storesAreTerminal = false;
            continue;
        }
        if (inst->is_store()) {
            hasStore = true;
            sawStore = true;
            continue;
        }
        auto *call = dynamic_cast<CallInst *>(inst);
        auto *callee = call ? dynamic_cast<Function *>(
                                  call->get_operand(call->num_ops_ - 1))
                            : nullptr;
        if (call && isSignedMinMaxIntrinsic(callee)) {
            if (call->num_ops_ != 3 ||
                call->get_operand(0)->type_ != call->type_ ||
                call->get_operand(1)->type_ != call->type_ ||
                !isSupportedSignedMinMaxType(call->type_))
                return reject(reason, "min/max intrinsic has unsupported type");
            if (sawStore) storesAreTerminal = false;
            continue;
        }

        auto *bin = dynamic_cast<BinaryInst *>(inst);
        if (!bin)
            return reject(reason, "loop contains an unsupported instruction");
        if (sawStore) storesAreTerminal = false;
        switch (bin->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
            break;
        default:
            return reject(reason, "arithmetic operation has no A53 vector lowering");
        }
        auto *integerType = dynamic_cast<IntegerType *>(bin->type_);
        if ((!integerType || integerType->num_bits_ != 32) &&
            bin->type_->tid_ != Type::FloatTyID)
            return reject(reason, "arithmetic result is not i32 or float");
    }
    if (!hasStore)
        return reject(reason, "element-wise loop has no observable vector store");
    plan.canDeferStoresAcrossParts = storesAreTerminal;

    for (auto *bb : plan.loop->blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() && inst->parent_ == plan.header) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !plan.loop->blocks.count(user->parent_))
                    return reject(reason, "non-phi loop value is live outside the loop");
            }
        }
    }
    return true;
}

bool LoopVectorizationAnalysis::checkMemoryDependences(
    Plan &plan, std::string *reason) const {
    std::set<std::pair<size_t, size_t>> checkedGroups;
    for (size_t i = 0; i < plan.memoryAccesses.size(); ++i) {
        const auto &a = plan.memoryAccesses[i];
        for (size_t j = i + 1; j < plan.memoryAccesses.size(); ++j) {
            const auto &b = plan.memoryAccesses[j];
            if (a.kind == AccessKind::Load && b.kind == AccessKind::Load)
                continue;
            AliasResult alias = BAA_.alias(memoryPointer(a.inst),
                                           memoryPointer(b.inst));
            auto isExpansionScratch = [](Value *object) {
                auto *alloca = dynamic_cast<AllocaInst *>(object);
                return alloca && alloca->isLoopExpansionScratch();
            };
            bool distinctExpansionScratch =
                a.underlyingObject && b.underlyingObject &&
                a.underlyingObject != b.underlyingObject &&
                (isExpansionScratch(a.underlyingObject) ||
                 isExpansionScratch(b.underlyingObject));
            if (alias == AliasResult::NoAlias || distinctExpansionScratch)
                continue;

            // Equal lane-wise addresses preserve the scalar program order:
            // each vector memory instruction still appears in that order and
            // distinct lanes address distinct elements.
            if (a.underlyingObject == b.underlyingObject &&
                sameAddressShape(a, b))
                continue;

            // Unit-stride non-uniform accesses describe contiguous ranges.
            // When static alias analysis cannot separate two such ranges, a
            // loop-versioning check can do so without weakening legality: the
            // original scalar loop remains the fallback for overlapping
            // ranges.  Record one check per normalized address-group pair.
            if (a.addressKind != AddressKind::Uniform &&
                b.addressKind != AddressKind::Uniform) {
                size_t firstGroup = std::min(a.addressGroup, b.addressGroup);
                size_t secondGroup = std::max(a.addressGroup, b.addressGroup);
                if (firstGroup != secondGroup &&
                    checkedGroups.emplace(firstGroup, secondGroup).second)
                    plan.runtimeMemoryChecks.push_back({i, j});
                continue;
            }
            return reject(reason, "possible loop-carried or cross-lane memory dependence");
        }
    }
    return true;
}

bool LoopVectorizationAnalysis::checkProfitability(Plan &plan,
                                                   std::string *reason) const {
    VectorizationCostModel costs;
    int scalarLaneCost = 0;
    int vectorPartCost = 0;
    int livePerPart = 0;
    std::set<Value *> splattedValues;
    std::set<size_t> addressGroups;

    for (const auto &access : plan.memoryAccesses)
        if (access.addressKind != AddressKind::Uniform)
            addressGroups.insert(access.addressGroup);

    for (auto *inst : plan.recipes) {
        if (inst->is_load() || inst->is_store() ||
            dynamic_cast<BinaryInst *>(inst) || dynamic_cast<CallInst *>(inst)) {
            scalarLaneCost += costs.scalarInstructionCost(inst);
            vectorPartCost += costs.vectorInstructionCost(inst);
            if (!inst->is_store()) ++livePerPart;
        }

        auto *binary = dynamic_cast<BinaryInst *>(inst);
        auto *call = dynamic_cast<CallInst *>(inst);
        if (!binary && !call) continue;
        unsigned valueOperands = call ? call->num_ops_ - 1 : inst->num_ops_;
        for (unsigned i = 0; i < valueOperands; ++i) {
            Value *operand = inst->get_operand(i);
            if (isLoopInvariant(operand, *plan.loop))
                splattedValues.insert(operand);
        }
    }

    if (scalarLaneCost <= 0 || vectorPartCost <= 0)
        return reject(reason, "vector body has no costed work");

    plan.setupCost = costs.setupCost(splattedValues.size(),
                                     plan.runtimeMemoryChecks.size(),
                                     addressGroups.size());
    const int persistentVectors = static_cast<int>(splattedValues.size()) +
                                  static_cast<int>(plan.pointerRecurrences.size()) +
                                  static_cast<int>(addressGroups.size());
    plan.estimatedLiveVectors = livePerPart + persistentVectors;

    // UF=2 is useful on this in-order target only while two copies of the
    // vector body leave ample room for persistent values and lowering
    // temporaries.  Otherwise prefer UF=1 rather than relying on later spills.
    const int unrolledLive = livePerPart * 2 + persistentVectors;
    if (unrolledLive <= costs.maximumUnrolledLiveVectors())
        plan.unrollFactor = 2;

    auto setCandidate = [&](int unrollFactor) {
        plan.unrollFactor = unrollFactor;
        const int width = plan.vectorWidth * unrollFactor;
        plan.scalarCost = scalarLaneCost * width;
        plan.vectorCost = vectorPartCost * unrollFactor +
                          costs.vectorLoopControlCost();
        plan.minimumTripCount = costs.minimumProfitableTripCount(
            scalarLaneCost, vectorPartCost, plan.setupCost, unrollFactor);
    };
    setCandidate(plan.unrollFactor);
    if (plan.minimumTripCount == 0 || plan.vectorCost >= plan.scalarCost)
        return reject(reason, "estimated vector cost is not lower than scalar cost");

    auto *init = dynamic_cast<ConstantInt *>(plan.induction.init);
    auto *bound = dynamic_cast<ConstantInt *>(plan.induction.bound);
    if (init && bound) {
        long long trip = static_cast<long long>(bound->value_) - init->value_;
        if (trip < plan.minimumTripCount && plan.unrollFactor == 2)
            setCandidate(1);
        if (trip < plan.minimumTripCount) {
            return reject(reason, "constant trip count is too small");
        }
    }
    return true;
}

bool LoopVectorizationAnalysis::buildPlan(Loop &loop, Plan &plan,
                                          std::string *reason) const {
    plan = Plan{};
    plan.loop = &loop;
    plan.preheader = loop.preheader;
    plan.header = loop.header;
    plan.latch = loop.singleLatch();
    plan.exit = loop.singleExit();
    if (loop.header && loop.header->hasSemFlag(SemFlag::VectorizedEpilogue))
        return reject(reason, "loop is already a vector epilogue");
    if (!plan.preheader || !plan.latch || !plan.exit)
        return reject(reason, "loop lacks dedicated preheader, latch, or exit");
    if (loop.blocks.empty() || loop.blocks.size() > 3)
        return reject(reason, "loop body is not a straight-line canonical body");

    auto *preTerm = plan.preheader->get_terminator();
    auto *headerTerm = plan.header->get_terminator();
    auto *latchTerm = plan.latch->get_terminator();
    if (!preTerm || !preTerm->is_br() || preTerm->num_ops_ != 1 ||
        preTerm->get_operand(0) != plan.header)
        return reject(reason, "preheader is not dedicated");
    if (!headerTerm || headerTerm->num_ops_ != 3 ||
        headerTerm->get_operand(2) != plan.exit)
        return reject(reason, "header branch is not canonical body/exit form");
    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    if (!bodyEntry || !loop.blocks.count(bodyEntry))
        return reject(reason, "header does not enter the loop body");
    const bool rotatedSingleBlock =
        plan.header == plan.latch && bodyEntry == plan.header &&
        loop.blocks.size() == 1;
    if (rotatedSingleBlock &&
        !plan.header->hasSemFlag(SemFlag::ScalarExpansionCompute))
        return reject(reason, "rotated single-block loop is not a proved scalar-expansion compute loop");
    plan.rotatedSingleBlock = rotatedSingleBlock;
    if (!rotatedSingleBlock &&
        (!latchTerm || !latchTerm->is_br() || latchTerm->num_ops_ != 1 ||
         latchTerm->get_operand(0) != plan.header))
        return reject(reason, "latch is not an unconditional backedge");
    if (rotatedSingleBlock) {
        plan.body = plan.header;
    } else if (bodyEntry != plan.latch) {
        auto *bodyTerm = bodyEntry->get_terminator();
        if (loop.blocks.size() != 3 || !bodyTerm || !bodyTerm->is_br() ||
            bodyTerm->num_ops_ != 1 || bodyTerm->get_operand(0) != plan.latch)
            return reject(reason, "loop body is not a straight-line canonical body");
    } else if (loop.blocks.size() != 2) {
        return reject(reason, "loop contains an unexpected extra block");
    }
    if (!plan.body) plan.body = bodyEntry;

    if (!findInduction(loop, plan, reason) ||
        !findPointerRecurrences(plan, reason))
        return false;

    std::vector<BasicBlock *> recipeBlocks{plan.header};
    if (plan.body != plan.header) recipeBlocks.push_back(plan.body);
    if (plan.latch != plan.body) recipeBlocks.push_back(plan.latch);
    for (auto *bb : recipeBlocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            plan.recipes.push_back(inst);
        }
    }

    if (!classifyMemory(plan, reason)) return false;
    if (plan.rotatedSingleBlock) {
        bool hasExpansionScratch = false;
        bool allI32 = true;
        for (const auto &access : plan.memoryAccesses) {
            auto *alloca = dynamic_cast<AllocaInst *>(access.underlyingObject);
            hasExpansionScratch |=
                alloca && alloca->isLoopExpansionScratch();
            auto *integer = dynamic_cast<IntegerType *>(access.scalarType);
            allI32 &= integer && integer->num_bits_ == 32;
        }
        if (!hasExpansionScratch || !allI32 ||
            plan.memoryAccesses.size() < 3)
            return reject(reason, "rotated scalar-expansion loop is not a profitable i32 scratch update");
    }

    return checkInstructions(plan, reason) &&
           checkMemoryDependences(plan, reason) &&
           checkProfitability(plan, reason);
}
