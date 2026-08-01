#include "../../../include/mid/opt/loopUnroll.hpp"
#include "../../../include/mid/opt/lcssa.hpp"
#include "../../../include/mid/analysis/moduloRecurrenceAnalysis.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

static const int DEFAULT_UNROLL_FACTOR = 4;
static const int MAX_STRUCTURED_LOOP_INSTS = 24;

struct UnrollCost {
    int bodyInstructions = 0;
    int memoryOperations = 0;
    bool hasVectorOperations = false;
    int integerStates = 0;
    int pointerStates = 0;
    int floatingStates = 0;
    int vectorStates = 0;
};

struct UnrolledModuloRecurrence {
    PhiInst *state = nullptr;
    BinaryInst *remainder = nullptr;
    ConstantInt *modulus = nullptr;
    std::vector<ModuloRecurrenceAnalysis::SignedTerm> contributionTerms;
    std::set<Instruction *> updateChain;
    long long contributionLower = 0;
    long long contributionUpper = 0;
    long long prefixLower = 0;
    long long prefixUpper = 0;
    long long finalLower = 0;
    long long finalUpper = 0;
};

static bool prepareModuloRecurrence(
    PhiInst *state, BinaryInst *remainder, Value *initialValue,
    const std::set<BasicBlock *> &updateBlocks,
    const std::vector<PhiInst *> &loopStates, PhiInst *inductionState,
    int unrollFactor, bool allowExternalUses,
    UnrolledModuloRecurrence &result) {
    if (unrollFactor <= 2)
        return false;

    ModuloRecurrenceAnalysis::Recurrence analyzed;
    if (!ModuloRecurrenceAnalysis::analyze(
            state, remainder, updateBlocks, analyzed) ||
        !ModuloRecurrenceAnalysis::hasPrivateUpdateChain(
            analyzed, updateBlocks, allowExternalUses))
        return false;

    UnrolledModuloRecurrence candidate;
    candidate.state = analyzed.state;
    candidate.remainder = analyzed.remainder;
    candidate.modulus = analyzed.modulus;
    candidate.contributionTerms = analyzed.contributionTerms;
    candidate.updateChain = analyzed.updateChain;
    if (!ModuloRecurrenceAnalysis::contributionBounds(
            analyzed, loopStates, inductionState,
            candidate.contributionLower, candidate.contributionUpper))
        return false;

    long long initLower = 0, initUpper = 0;
    if (!ModuloRecurrenceAnalysis::inferBounds(
            initialValue, initLower, initUpper))
        return false;

    auto advanceByTerms = [&](long long &lower, long long &upper) {
        for (const auto &term : candidate.contributionTerms) {
            long long termLower = 0, termUpper = 0;
            if (!ModuloRecurrenceAnalysis::inferBounds(
                    term.value, termLower, termUpper))
                return false;
            __int128 nextLower = term.sign > 0
                                     ? static_cast<__int128>(lower) + termLower
                                     : static_cast<__int128>(lower) - termUpper;
            __int128 nextUpper = term.sign > 0
                                     ? static_cast<__int128>(upper) + termUpper
                                     : static_cast<__int128>(upper) - termLower;
            if (nextLower < std::numeric_limits<int>::min() ||
                nextUpper > std::numeric_limits<int>::max())
                return false;
            lower = static_cast<long long>(nextLower);
            upper = static_cast<long long>(nextUpper);
        }
        return true;
    };

    const long long modulus = candidate.modulus->value_;
    candidate.prefixLower = std::min(initLower, -modulus + 1);
    candidate.prefixUpper = std::max(initUpper, modulus - 1);
    for (int prefix = 1; prefix < unrollFactor; ++prefix)
        if (!advanceByTerms(candidate.prefixLower,
                            candidate.prefixUpper))
            return false;

    candidate.finalLower = -modulus + 1;
    candidate.finalUpper = modulus - 1;
    if (!advanceByTerms(candidate.finalLower, candidate.finalUpper))
        return false;
    if (!ModuloRecurrenceAnalysis::needsAtMostOneCorrection(
            candidate.prefixLower, candidate.prefixUpper, modulus) ||
        !ModuloRecurrenceAnalysis::needsAtMostOneCorrection(
            candidate.finalLower, candidate.finalUpper, modulus))
        return false;

    result = std::move(candidate);
    return true;
}

static bool isMustExecuteModuloRecurrence(
    const UnrolledModuloRecurrence &recurrence, const Loop &loop,
    Function *func, BasicBlock *latch) {
    if (!func || !latch)
        return false;
    for (Instruction *instruction : recurrence.updateChain) {
        if (!instruction->parent_ ||
            !loop.blocks.count(instruction->parent_) ||
            !func->dominates(instruction->parent_, latch))
            return false;
    }
    for (const auto &term : recurrence.contributionTerms) {
        auto *instruction = dynamic_cast<Instruction *>(term.value);
        if (instruction && loop.blocks.count(instruction->parent_) &&
            !func->dominates(instruction->parent_, latch))
            return false;
    }
    return true;
}

static Value *buildBoundedModulo(Value *dividend, long long lower,
                                 long long upper, int modulus,
                                 Module *module, BasicBlock *block) {
    Value *adjusted = dividend;
    if (upper >= modulus) {
        auto *positiveMod = new ConstantInt(module->int32_ty_, modulus);
        auto *highCmp = new ICmpInst(ICmpInst::ICMP_SGE, adjusted,
                                     positiveMod, block);
        auto *highSub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                       adjusted, positiveMod, block);
        adjusted = new SelectInst(highCmp, highSub, adjusted, block);
    }
    if (lower <= -modulus) {
        auto *negativeMod = new ConstantInt(module->int32_ty_, -modulus);
        auto *positiveMod = new ConstantInt(module->int32_ty_, modulus);
        auto *lowCmp = new ICmpInst(ICmpInst::ICMP_SLE, adjusted,
                                    negativeMod, block);
        auto *lowAdd = new BinaryInst(module->int32_ty_, Instruction::Add,
                                      adjusted, positiveMod, block);
        adjusted = new SelectInst(lowCmp, lowAdd, adjusted, block);
    }
    return adjusted;
}

static bool computeBoundAdjustment(int iterations, int stride,
                                   int &adjustment) {
    long long wide = static_cast<long long>(iterations) * stride;
    if (wide < std::numeric_limits<int>::min() ||
        wide > std::numeric_limits<int>::max())
        return false;
    adjustment = static_cast<int>(wide);
    return true;
}

// `bound - adjustment` is evaluated with i32 arithmetic.  When the bound is
// dynamic, guard the unrolled path so a wrapped adjusted bound cannot turn a
// zero-trip loop into a very large loop.  The original loop remains the
// fallback and therefore handles every value outside this safe interval.
static Value *buildBoundAdjustmentGuard(Value *bound, int adjustment,
                                        Module *module, BasicBlock *block) {
    if (adjustment > 0) {
        auto *lowerLimit = new ConstantInt(
            module->int32_ty_,
            std::numeric_limits<int>::min() + adjustment);
        return new ICmpInst(ICmpInst::ICMP_SGE, bound, lowerLimit, block);
    }
    if (adjustment < 0) {
        auto *upperLimit = new ConstantInt(
            module->int32_ty_,
            std::numeric_limits<int>::max() + adjustment);
        return new ICmpInst(ICmpInst::ICMP_SLE, bound, upperLimit, block);
    }
    return nullptr;
}

static Value *guardCondition(Value *condition, Value *guard, Module *module,
                             BasicBlock *block) {
    if (!guard)
        return condition;
    return new BinaryInst(module->int1_ty_, Instruction::And, guard,
                          condition, block);
}

// Choose an unroll factor from target-independent loop facts and the A53
// register/code-size budget.  Eight-way unrolling is reserved for compact,
// register-only scalar loops: it amortizes loop control and exposes enough
// independent work for the dual-issue core without multiplying memory traffic
// or pointer live ranges.  General scalar loops retain the four-way default;
// vector or high pointer-pressure loops use two-way unrolling.
static int chooseUnrollFactor(const UnrollCost &cost) {
    if (cost.bodyInstructions <= 0)
        return 0;

    int gprPeak =
        2 * (cost.integerStates + cost.pointerStates) + 2;
    int fprPeak =
        2 * (cost.floatingStates + cost.vectorStates);
    bool highPointerPressure =
        cost.pointerStates >= 2 && cost.integerStates > 0;

    if (cost.hasVectorOperations) {
        if (cost.bodyInstructions <= 12 &&
            cost.bodyInstructions * 2 <= 24 &&
            gprPeak <= 26 && fprPeak <= 28)
            return 2;
        return 0;
    }

    bool registerOnly =
        cost.memoryOperations == 0 && cost.pointerStates == 0;
    if (!registerOnly) {
        // Keep memory-loop growth conservative.  Replicating a large scalar
        // memory body increases A53 load/store pressure and code footprint;
        // the scheduler cannot recover that cost.  This retains the previous
        // eight-instruction eligibility boundary while still using the common
        // pressure model to select two-way versus four-way expansion.
        if (cost.bodyInstructions > 8)
            return 0;
        int factor =
            highPointerPressure ? 2 : DEFAULT_UNROLL_FACTOR;
        if (cost.bodyInstructions * factor > 32 ||
            gprPeak > 24 || fprPeak > 24)
            return 0;
        return factor;
    }

    bool enoughScalarWorkForEight =
        cost.bodyInstructions >= 6 || cost.floatingStates > 0;
    if (enoughScalarWorkForEight &&
        cost.bodyInstructions <= 9 &&
        cost.bodyInstructions * 8 <= 72 &&
        gprPeak <= 20 && fprPeak <= 20)
        return 8;

    if (cost.bodyInstructions <= 8 &&
        cost.bodyInstructions * DEFAULT_UNROLL_FACTOR <= 48 &&
        gprPeak <= 24 && fprPeak <= 24)
        return DEFAULT_UNROLL_FACTOR;

    // Larger register-only arithmetic loops can still benefit from exposing
    // one independent successor iteration.  Keep this at two copies: it is
    // enough to hide multi-cycle multiply/divide chains on an in-order A53,
    // while a wider expansion would exceed the available GPR budget.
    if (cost.bodyInstructions <= 28 &&
        cost.bodyInstructions * 2 <= 56 &&
        gprPeak <= 20 && fprPeak <= 20)
        return 2;
    return 0;
}

static void countLoopStates(const std::vector<PhiInst *> &headerPhis,
                            PhiInst *ivPhi, UnrollCost &cost) {
    for (auto *phi : headerPhis) {
        if (phi == ivPhi)
            continue;
        switch (phi->type_->tid_) {
        case Type::IntegerTyID:
            ++cost.integerStates;
            break;
        case Type::PointerTyID:
            ++cost.pointerStates;
            break;
        case Type::FloatTyID:
            ++cost.floatingStates;
            break;
        case Type::VectorTyID:
            ++cost.vectorStates;
            break;
        default:
            break;
        }
    }
}

static bool debugStructuredReject(Function *func, Loop &loop,
                                  const char *reason) {
    if (std::getenv("DEBUG_LOOP_UNROLL")) {
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << (loop.header ? loop.header->name_ : "<none>")
                  << " structured-reject=" << reason << "\n";
    }
    return false;
}

static bool debugCFGRegionReject(Function *func, Loop &loop,
                                 const char *reason) {
    if (std::getenv("DEBUG_LOOP_UNROLL")) {
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << (loop.header ? loop.header->name_ : "<none>")
                  << " cfg-region-reject=" << reason << "\n";
    }
    return false;
}

// A two-way vector unroll initially keeps each iteration's store next to its
// computation.  When alias analysis proves that the first store cannot affect
// any load in the second iteration, sink it next to the second store.  This
// exposes both load/compute chains to the machine scheduler without changing
// memory order unless the crossed accesses are proven disjoint.
static bool clusterTwoVectorStores(BasicBlock *body,
                                   BasicAliasAnalysis &BAA) {
    if (!body) return false;

    std::vector<std::list<Instruction *>::iterator> stores;
    for (auto it = body->instr_list_.begin(); it != body->instr_list_.end(); ++it) {
        auto *store = dynamic_cast<StoreInst *>(*it);
        if (!store || store->get_operand(0)->type_->tid_ != Type::VectorTyID)
            continue;
        stores.push_back(it);
    }
    if (stores.size() != 2) return false;

    auto first = stores[0];
    auto second = stores[1];
    Value *storedPtr = (*first)->get_operand(1);
    for (auto it = std::next(first); it != second; ++it) {
        Instruction *inst = *it;
        if (inst->is_call() || inst->is_store()) return false;
        if (!inst->is_load()) continue;
        if (BAA.alias(storedPtr, inst->get_operand(0)) != AliasResult::NoAlias)
            return false;
    }

    body->instr_list_.splice(second, body->instr_list_, first);
    return true;
}

// ── Instruction cloning ───────────────────────────────────────────────────

Instruction *LoopUnroll::cloneInst(Instruction *orig, BasicBlock *destBB,
                                    const std::unordered_map<Value *, Value *> &vmap) {
    auto remap = [&](Value *v) -> Value * {
        auto it = vmap.find(v);
        return it != vmap.end() ? it->second : v;
    };

    if (auto *bi = dynamic_cast<BinaryInst *>(orig))
        {
            auto *inst = new BinaryInst(bi->type_, bi->op_id_,
                                        remap(bi->get_operand(0)),
                                        remap(bi->get_operand(1)), destBB);
            inst->copySemFlagsFrom(bi);
            return inst;
        }

    if (auto *ui = dynamic_cast<UnaryInst *>(orig))
        {
            auto *inst = new UnaryInst(ui->type_, ui->op_id_,
                                       remap(ui->get_operand(0)), destBB);
            inst->copySemFlagsFrom(ui);
            return inst;
        }

    if (auto *ci = dynamic_cast<ICmpInst *>(orig))
        {
            auto *inst = new ICmpInst(ci->icmp_op_,
                                      remap(ci->get_operand(0)),
                                      remap(ci->get_operand(1)), destBB);
            inst->copySemFlagsFrom(ci);
            return inst;
        }

    if (auto *fi = dynamic_cast<FCmpInst *>(orig))
        {
            auto *inst = new FCmpInst(fi->fcmp_op_,
                                      remap(fi->get_operand(0)),
                                      remap(fi->get_operand(1)), destBB);
            inst->copySemFlagsFrom(fi);
            return inst;
        }

    if (auto *si = dynamic_cast<SelectInst *>(orig))
        {
            auto *inst = new SelectInst(remap(si->get_operand(0)),
                                        remap(si->get_operand(1)),
                                        remap(si->get_operand(2)),
                                        si->type_);
            inst->copySemFlagsFrom(si);
            destBB->add_instruction(inst);
            return inst;
        }

    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remap(gi->get_operand(i)));
        auto *inst = new GetElementPtrInst(remap(gi->get_operand(0)), idxs, destBB);
        inst->copySemFlagsFrom(gi);
        return inst;
    }

    if (auto *li = dynamic_cast<LoadInst *>(orig))
        {
            auto *inst = new LoadInst(remap(li->get_operand(0)), destBB);
            inst->copySemFlagsFrom(li);
            return inst;
        }

    if (auto *si = dynamic_cast<StoreInst *>(orig))
        {
            auto *inst = new StoreInst(remap(si->get_operand(0)),
                                       remap(si->get_operand(1)), destBB);
            inst->copySemFlagsFrom(si);
            return inst;
        }

    if (auto *zi = dynamic_cast<ZextInst *>(orig))
        {
            auto *inst = new ZextInst(zi->op_id_, remap(zi->get_operand(0)),
                                      zi->dest_ty_, destBB);
            inst->copySemFlagsFrom(zi);
            return inst;
        }

    if (auto *fp = dynamic_cast<FpToSiInst *>(orig))
        {
            auto *inst = new FpToSiInst(fp->op_id_, remap(fp->get_operand(0)),
                                        fp->dest_ty_, destBB);
            inst->copySemFlagsFrom(fp);
            return inst;
        }

    if (auto *sf = dynamic_cast<SiToFpInst *>(orig))
        {
            auto *inst = new SiToFpInst(sf->op_id_, remap(sf->get_operand(0)),
                                        sf->dest_ty_, destBB);
            inst->copySemFlagsFrom(sf);
            return inst;
        }

    if (auto *bc = dynamic_cast<Bitcast *>(orig))
        {
            auto *inst = new Bitcast(bc->op_id_, remap(bc->get_operand(0)),
                                     bc->dest_ty_, destBB);
            inst->copySemFlagsFrom(bc);
            return inst;
        }

    if (auto *sel = dynamic_cast<SelectInst *>(orig))
        {
            auto *inst = new SelectInst(remap(sel->get_operand(0)),
                                        remap(sel->get_operand(1)),
                                        remap(sel->get_operand(2)), destBB);
            inst->copySemFlagsFrom(sel);
            return inst;
        }

    return nullptr; // unsupported (phi, branch, call, alloca …)
}

static Value *mapLoopValue(
    Value *val,
    const std::unordered_map<Value *, Value *> &valueMap,
    const std::unordered_map<BasicBlock *, BasicBlock *> &bbMap) {
    if (auto it = valueMap.find(val); it != valueMap.end())
        return it->second;
    if (auto *bb = dynamic_cast<BasicBlock *>(val)) {
        auto bit = bbMap.find(bb);
        return bit == bbMap.end() ? nullptr : bit->second;
    }
    return val;
}

static bool isCloneableForUnroll(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<SelectInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<StoreInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<SelectInst *>(inst);
}

static bool dependsOnAlloca(Value *value, std::set<Value *> &visited) {
    if (!value || !visited.insert(value).second)
        return false;
    if (dynamic_cast<AllocaInst *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst)
        return false;
    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        if (dynamic_cast<BasicBlock *>(inst->get_operand(i)))
            continue;
        if (dependsOnAlloca(inst->get_operand(i), visited))
            return true;
    }
    return false;
}

static bool dependsOnAlloca(Value *value) {
    std::set<Value *> visited;
    return dependsOnAlloca(value, visited);
}

static bool hasEntryLowerBoundGuard(BasicBlock *preheader, BasicBlock *header,
                                    Value *bound, ConstantInt *init) {
    auto *br = dynamic_cast<BranchInst *>(preheader ? preheader->get_terminator() : nullptr);
    if (!br || !init) return false;
    if (br->num_ops_ == 1 && br->get_operand(0) == header &&
        preheader->pre_bbs_.size() == 1) {
        return hasEntryLowerBoundGuard(preheader->pre_bbs_[0], preheader,
                                       bound, init);
    }
    if (br->num_ops_ != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(br->get_operand(0));
    if (!cmp) return false;

    bool headerOnTrue = br->get_operand(1) == header;
    bool headerOnFalse = br->get_operand(2) == header;
    if (headerOnTrue == headerOnFalse) return false;

    auto sameConst = [&](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == init->value_;
    };

    auto provesOnTrue = [&]() {
        if (cmp->icmp_op_ == ICmpInst::ICMP_SGE &&
            cmp->get_operand(0) == bound && sameConst(cmp->get_operand(1)))
            return true;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SLE &&
            sameConst(cmp->get_operand(0)) && cmp->get_operand(1) == bound)
            return true;
        return false;
    };
    auto provesOnFalse = [&]() {
        if (cmp->icmp_op_ == ICmpInst::ICMP_SLT &&
            cmp->get_operand(0) == bound && sameConst(cmp->get_operand(1)))
            return true;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SGT &&
            sameConst(cmp->get_operand(0)) && cmp->get_operand(1) == bound)
            return true;
        return false;
    };

    return (headerOnTrue && provesOnTrue()) || (headerOnFalse && provesOnFalse());
}

static bool hasNestedLoopBackedge(const Loop &loop) {
    for (auto *bb : loop.blocksOrdered) {
        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br) continue;
        for (unsigned i = 0; i < br->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock *>(br->get_operand(i));
            if (!succ || succ == loop.header) continue;
            if (loop.blocks.count(succ))
                return true;
        }
    }
    return false;
}

static bool isProfitableCFGRegionUnroll(const Loop &loop, int bodyInstCount,
                                        int condBranchBlocks, int memoryOps,
                                        int vectorOps) {
    if (bodyInstCount <= 0) return false;

    // CFG-region unroll clones whole inner control flow.  On Cortex-A53 this is
    // only worthwhile when the cloned region exposes real memory/vector work;
    // pure scalar branch nests usually lose to I-cache/BTB pressure.
    if (hasNestedLoopBackedge(loop) && memoryOps == 0 && vectorOps == 0)
        return false;

    if (condBranchBlocks * 3 > bodyInstCount && memoryOps == 0 && vectorOps == 0)
        return false;

    return true;
}

// ── Core unrolling ────────────────────────────────────────────────────────

bool LoopUnroll::tryUnroll(Loop &loop, Function *func, Module *module,
                           BasicAliasAnalysis &BAA) {
    // Only handle simple 2-BB loops: header + latch
    if (loop.blocks.size() != 2) return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch  = loop.singleLatch();
    if (!latch || latch == header) return false;

    // Need a single preheader
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;

    // Need a single exit from the header
    BasicBlock *exitBB = nullptr;
    for (auto succ : header->succ_bbs_) {
        if (!loop.blocks.count(succ)) {
            if (exitBB) return false;
            exitBB = succ;
        }
    }
    if (!exitBB) return false;

    // Collect header phis
    std::vector<PhiInst *> headerPhis;
    for (auto inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        headerPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (headerPhis.empty()) return false;

    // Find IV: integer phi whose latch-incoming value is
    //   phi + ConstantInt stride (stride > 0)   — forward loop, or
    //   phi - ConstantInt stride (stride > 0)   — reverse loop
    PhiInst *    ivPhi    = nullptr;
    ConstantInt *stride   = nullptr;  // > 0 for add, < 0 for sub
    Instruction *ivUpdate = nullptr;

    for (auto phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID) continue;

        for (auto inst : latch->instr_list_) {
            if (!inst->is_add() && !inst->is_sub()) continue;
            Value *op0 = inst->get_operand(0);
            Value *op1 = inst->get_operand(1);

            if (inst->is_add()) {
                // add phi, c  or  add c, phi
                auto *c0 = dynamic_cast<ConstantInt *>(op0);
                auto *c1 = dynamic_cast<ConstantInt *>(op1);
                ConstantInt *c   = c1 ? c1 : c0;
                Value *      base = c1 ? op0 : op1;
                if (!c || c->value_ <= 0 || base != phi) continue;
                stride = c;
            } else {
                // sub phi, c   (reverse loop: stride = -c)
                auto *c1 = dynamic_cast<ConstantInt *>(op1);
                if (!c1 || c1->value_ <= 0 || op0 != phi) continue;
                stride = new ConstantInt(phi->type_, -c1->value_);
            }

            // Verify this instruction feeds back into the phi
            bool feedsBack = false;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i) == inst) { feedsBack = true; break; }
            }
            if (!feedsBack) continue;

            ivPhi    = phi;
            ivUpdate = inst;
            break;
        }
        if (ivPhi) break;
    }
    if (!ivPhi) return false;

    // Find condition: icmp using iv, loop-invariant bound, slt/sle/sgt/sge
    ICmpInst *cmpInst  = nullptr;
    Value *   bound    = nullptr;
    bool      ivIsLeft = true;

    for (auto inst : header->instr_list_) {
        if (!inst->is_cmp()) continue;
        auto *cmp = static_cast<ICmpInst *>(inst);
        auto  op  = cmp->icmp_op_;
        if (op != ICmpInst::ICMP_SLT && op != ICmpInst::ICMP_SLE &&
            op != ICmpInst::ICMP_SGT && op != ICmpInst::ICMP_SGE)
            break;
        if (cmp->get_operand(0) == ivPhi) {
            bound = cmp->get_operand(1); ivIsLeft = true;  cmpInst = cmp;
        } else if (cmp->get_operand(1) == ivPhi) {
            bound = cmp->get_operand(0); ivIsLeft = false; cmpInst = cmp;
        }
        break;
    }
    if (!cmpInst) return false;

    // Bound must be loop-invariant
    if (auto *bi = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(bi->parent_)) return false;

    // Determine which branch successor is the body vs exit
    auto *headerBr = header->get_terminator();
    if (!headerBr || !headerBr->is_br() || headerBr->num_ops_ != 3) return false;
    // The compare selected above must be the loop's actual continue
    // condition.  A header may contain auxiliary range/overflow compares
    // combined by an `and`; adjusting one of those as if it were the branch
    // condition discards the other guard and can turn a bounded loop into an
    // effectively unbounded one.
    if (headerBr->get_operand(0) != cmpInst) return false;
    auto *trueSucc = static_cast<BasicBlock *>(headerBr->get_operand(1));
    // true → body means the condition is "continue loop" (forward loop)
    if (!loop.blocks.count(trueSucc)) return false;

    // No calls/phis/allocas in latch; only handle instruction types we can clone;
    // also skip loops whose body is large enough to cause register pressure when
    // unrolled (Cortex-A53 has limited registers and no OOO execution).
    UnrollCost unrollCost;
    for (auto inst : latch->instr_list_) {
        if (inst->isTerminator()) continue;
        if (inst->is_call() || inst->is_phi() || inst->is_alloca()) return false;
        ++unrollCost.bodyInstructions;
        if (inst->is_load() || inst->is_store() || inst->is_gep())
            ++unrollCost.memoryOperations;
        if (inst->type_->tid_ == Type::VectorTyID)
            unrollCost.hasVectorOperations = true;
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            if (inst->get_operand(i)->type_->tid_ == Type::VectorTyID) {
                unrollCost.hasVectorOperations = true;
                break;
            }
        }
        bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                        dynamic_cast<UnaryInst *>(inst) ||
                        dynamic_cast<ICmpInst *>(inst) ||
                        dynamic_cast<FCmpInst *>(inst) ||
                        dynamic_cast<SelectInst *>(inst) ||
                        dynamic_cast<GetElementPtrInst *>(inst) ||
                        dynamic_cast<LoadInst *>(inst) ||
                        dynamic_cast<StoreInst *>(inst) ||
                        dynamic_cast<ZextInst *>(inst) ||
                        dynamic_cast<FpToSiInst *>(inst) ||
                        dynamic_cast<SiToFpInst *>(inst) ||
                        dynamic_cast<Bitcast *>(inst);
        if (!canClone) return false;
    }
    countLoopStates(headerPhis, ivPhi, unrollCost);
    int effectiveUnrollFactor = chooseUnrollFactor(unrollCost);
    if (effectiveUnrollFactor == 0)
        return false;

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << effectiveUnrollFactor
                  << " body=" << unrollCost.bodyInstructions
                  << " gpr-state="
                  << unrollCost.integerStates + unrollCost.pointerStates
                  << " fpr-state="
                  << unrollCost.floatingStates + unrollCost.vectorStates
                  << " memory=" << unrollCost.memoryOperations << "\n";

    // ── Transformation ────────────────────────────────────────────────────

    int N   = effectiveUnrollFactor;
    int s   = stride->value_;
    int adj = 0;
    if (!computeBoundAdjustment(N - 1, s, adj))
        return false;

    // Guard against integer underflow: if the loop bound is smaller than the
    // adjustment, bound - adj would go negative and wrap to a huge unsigned
    // value (e.g. 0xFFFFFFFF for -1), making the main loop condition always
    // true → infinite loop with out-of-bounds memory access.
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (cb->value_ < adj) return false;
    }

    // Helper: get the preheader-incoming value of a phi
    auto getInitVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == preheader)
                return phi->get_operand(i);
        return nullptr;
    };

    // Helper: get the latch-incoming value of a phi
    auto getLatchVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (loop.blocks.count(static_cast<BasicBlock *>(phi->get_operand(i + 1))))
                return phi->get_operand(i);
        return nullptr;
    };

    std::vector<UnrolledModuloRecurrence> modularRecurrences;
    std::unordered_map<PhiInst *, std::size_t> modularRecurrenceIndex;
    std::set<Instruction *> modularUpdateInstructions;
    if (N > 2) {
        std::set<BasicBlock *> updateBlocks{latch};
        for (PhiInst *phi : headerPhis) {
            if (phi == ivPhi || phi->type_->tid_ != Type::IntegerTyID)
                continue;
            auto *remainder = dynamic_cast<BinaryInst *>(getLatchVal(phi));
            UnrolledModuloRecurrence candidate;
            if (!remainder ||
                !prepareModuloRecurrence(
                    phi, remainder, getInitVal(phi), updateBlocks,
                    headerPhis, ivPhi, N, false, candidate))
                continue;
            modularRecurrenceIndex[phi] = modularRecurrences.size();
            modularUpdateInstructions.insert(
                candidate.updateChain.begin(), candidate.updateChain.end());
            modularRecurrences.push_back(std::move(candidate));
            if (std::getenv("DEBUG_LOOP_UNROLL"))
                std::cerr << "[LoopUnroll] func=" << func->name_
                          << " header=" << header->name_
                          << " modular-prefix=" << N - 1 << "\n";
        }
    }

    // 1. Compute adjusted bound (for the main unrolled loop check)
    Value *boundMain;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjConst = new ConstantInt(module->int32_ty_, adj);
        auto *subInst  = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                         bound, adjConst, preheader, /*no-insert*/true);
        preheader->add_instruction_before_terminator(subInst);
        boundMain = subInst;
    }

    // 2. Create headerMain with phi nodes (added in reverse to maintain order)
    BasicBlock *headerMain = new BasicBlock(module, "unroll_hdr", func);

    std::unordered_map<PhiInst *, PhiInst *> phiToMain;
    for (int i = (int)headerPhis.size() - 1; i >= 0; i--) {
        auto *phi     = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(getInitVal(phi), preheader);
        // back-edge incoming (from unrolledBody) added later
        phiToMain[phi] = mainPhi;
    }

    // Condition in headerMain: iv < bound - adj  (same icmp op, adjusted bound)
    PhiInst *ivMain = phiToMain[ivPhi];
    ICmpInst *cmpMain;
    if (ivIsLeft)
        cmpMain = new ICmpInst(cmpInst->icmp_op_, ivMain, boundMain, headerMain);
    else
        cmpMain = new ICmpInst(cmpInst->icmp_op_, boundMain, ivMain, headerMain);
    Value *mainCondition = cmpMain;
    if (!dynamic_cast<ConstantInt *>(bound))
        mainCondition = guardCondition(
            cmpMain,
            buildBoundAdjustmentGuard(bound, adj, module, headerMain),
            module, headerMain);

    // 3. Build unrolledBody: N copies of the latch (non-terminator instructions)
    BasicBlock *unrolledBody = new BasicBlock(module, "unroll_body", func);

    // Initial value map: header phis → their main-loop phi counterparts
    std::unordered_map<Value *, Value *> iterMap;
    for (auto phi : headerPhis)
        iterMap[phi] = phiToMain[phi];

    // Also seed with any latch-defined values that feed back into phis,
    // so iterations chain correctly
    std::unordered_map<PhiInst *, Value *> curPhiVals;
    for (auto phi : headerPhis)
        curPhiVals[phi] = phiToMain[phi];

    std::unordered_map<PhiInst *, Value *> modularAccumulators;
    for (const auto &recurrence : modularRecurrences)
        modularAccumulators[recurrence.state] =
            phiToMain[recurrence.state];

    for (int iter = 0; iter < N; iter++) {
        std::unordered_map<Value *, Value *> localMap = iterMap;

        for (auto inst : latch->instr_list_) {
            if (inst->isTerminator()) continue;
            if (iter < N - 1 &&
                modularUpdateInstructions.count(inst))
                continue;
            auto *newInst = cloneInst(inst, unrolledBody, localMap);
            if (!newInst) return false; // should not happen after pre-check
            localMap[inst] = newInst;
        }

        if (iter < N - 1) {
            for (const auto &recurrence : modularRecurrences) {
                Value *accumulator =
                    modularAccumulators[recurrence.state];
                for (const auto &term : recurrence.contributionTerms) {
                    auto mapped = localMap.find(term.value);
                    Value *contribution =
                        mapped != localMap.end() ? mapped->second : term.value;
                    accumulator = new BinaryInst(
                        module->int32_ty_,
                        term.sign > 0 ? Instruction::Add : Instruction::Sub,
                        accumulator, contribution, unrolledBody);
                }
                modularAccumulators[recurrence.state] = accumulator;
                if (iter == N - 2) {
                    Value *combined = buildBoundedModulo(
                        accumulator, recurrence.prefixLower,
                        recurrence.prefixUpper,
                        recurrence.modulus->value_, module, unrolledBody);
                    curPhiVals[recurrence.state] = combined;
                }
            }
        }

        // Update iterMap for next iteration: replace each phi's "current" value
        // with what comes out of the latch update this iteration
        for (auto phi : headerPhis) {
            if (iter < N - 1 && modularRecurrenceIndex.count(phi))
                continue;
            Value *lv = getLatchVal(phi);
            if (!lv || !localMap.count(lv))
                continue;
            Value *nextValue = localMap[lv];
            auto recurrenceIt = modularRecurrenceIndex.find(phi);
            if (iter == N - 1 &&
                recurrenceIt != modularRecurrenceIndex.end()) {
                const auto &recurrence =
                    modularRecurrences[recurrenceIt->second];
                auto *finalRemainder =
                    dynamic_cast<BinaryInst *>(nextValue);
                if (!finalRemainder ||
                    finalRemainder->op_id_ != Instruction::SRem)
                    return false;
                nextValue = buildBoundedModulo(
                    finalRemainder->get_operand(0),
                    recurrence.finalLower, recurrence.finalUpper,
                    recurrence.modulus->value_, module, unrolledBody);
                localMap[lv] = nextValue;
            }
            curPhiVals[phi] = nextValue;
        }
        // Next iteration uses the outputs of this one
        for (auto phi : headerPhis)
            iterMap[phi] = curPhiVals[phi];
    }

    if (N == 2 && unrollCost.hasVectorOperations)
        clusterTwoVectorStores(unrolledBody, BAA);

    // 4. Branch in unrolledBody → headerMain (back-edge)
    new BranchInst(headerMain, unrolledBody);

    // 5. Now fill in the back-edge incoming values for headerMain phis
    for (auto phi : headerPhis)
        phiToMain[phi]->addIncoming(curPhiVals[phi], unrolledBody);

    // 6. Conditional branch in headerMain.  Exit through a single-predecessor
    // remainder entry so the newly created main loop keeps dedicated exits.
    BasicBlock *remEntry = new BasicBlock(module, "unroll_rem_entry", func);
    new BranchInst(header, remEntry);
    new BranchInst(mainCondition, unrolledBody, remEntry, headerMain);

    // 7. Update original header phis: change preheader-incoming →
    // [mainPhiVal, remEntry]
    for (auto phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) != preheader) continue;
            // Remove old uses
            phi->get_operand(i)->remove_use(phi->use_pos_[i]);
            phi->get_operand(i + 1)->remove_use(phi->use_pos_[i + 1]);
            // Set new incoming: value = mainPhi, block = headerMain
            phi->operands_[i]     = phiToMain[phi];
            phi->use_pos_[i]      = phiToMain[phi]->add_use(phi, i);
            phi->operands_[i + 1] = remEntry;
            phi->use_pos_[i + 1]  = remEntry->add_use(phi, i + 1);
            break;
        }
    }

    // 8. Redirect preheader → headerMain (was → header)
    auto *preheaderBr = preheader->get_terminator();
    for (unsigned i = 0; i < preheaderBr->num_ops_; i++) {
        if (preheaderBr->get_operand(i) != header) continue;
        preheaderBr->get_operand(i)->remove_use(preheaderBr->use_pos_[i]);
        preheaderBr->operands_[i] = headerMain;
        preheaderBr->use_pos_[i]  = headerMain->add_use(preheaderBr, i);
        break;
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    return true;
}

bool LoopUnroll::tryUnrollStructured(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() <= 2)
        return false;
    if (loop.blocks.size() > 4)
        return debugStructuredReject(func, loop, "too-many-blocks");

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exitBB = loop.singleExit();
    if (!header || !latch || !preheader || !exitBB)
        return debugStructuredReject(func, loop, "missing-structural-block");
    if (loop.exiting.size() != 1 || loop.exiting[0] != latch)
        return debugStructuredReject(func, loop, "multiple-exiting-blocks");

    auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchTerm || latchTerm->num_ops_ != 3)
        return debugStructuredReject(func, loop, "latch-not-cond-branch");
    auto *continueSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    auto *exitSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
    if (continueSucc != header || exitSucc != exitBB) {
        continueSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
        exitSucc = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    }
    if (continueSucc != header || exitSucc != exitBB)
        return debugStructuredReject(func, loop, "latch-successors");

    std::vector<PhiInst *> headerPhis;
    std::unordered_map<PhiInst *, Value *> initVals;
    std::unordered_map<PhiInst *, Value *> latchVals;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4)
            return debugStructuredReject(func, loop, "non-canonical-header-phi");
        Value *init = nullptr;
        Value *back = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == preheader)
                init = phi->get_operand(i);
            else if (src == latch)
                back = phi->get_operand(i);
            else
                return debugStructuredReject(func, loop, "header-phi-not-preheader-latch");
        }
        if (!init || !back)
            return debugStructuredReject(func, loop, "header-phi-missing-edge");
        headerPhis.push_back(phi);
        initVals[phi] = init;
        latchVals[phi] = back;
    }
    if (headerPhis.empty())
        return debugStructuredReject(func, loop, "no-header-phis");

    auto *cmpInst = dynamic_cast<ICmpInst *>(latchTerm->get_operand(0));
    if (!cmpInst)
        return debugStructuredReject(func, loop, "latch-no-icmp");
    auto pred = cmpInst->icmp_op_;
    if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE &&
        pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
        return debugStructuredReject(func, loop, "unsupported-predicate");

    PhiInst *ivPhi = nullptr;
    int strideVal = 0;
    bool ivIsLeft = true;
    Value *bound = nullptr;
    for (auto *phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *upd = dynamic_cast<Instruction *>(latchVals[phi]);
        if (!upd || upd->parent_ != latch || (!upd->is_add() && !upd->is_sub()))
            continue;
        if (upd->is_add()) {
            auto *c0 = dynamic_cast<ConstantInt *>(upd->get_operand(0));
            auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
            if (upd->get_operand(0) == phi && c1 && c1->value_ > 0)
                strideVal = c1->value_;
            else if (upd->get_operand(1) == phi && c0 && c0->value_ > 0)
                strideVal = c0->value_;
            else
                continue;
        } else {
            auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
            if (upd->get_operand(0) != phi || !c1 || c1->value_ <= 0)
                continue;
            strideVal = -c1->value_;
        }

        if (cmpInst->get_operand(0) == phi || cmpInst->get_operand(0) == upd) {
            ivIsLeft = true;
            bound = cmpInst->get_operand(1);
        } else if (cmpInst->get_operand(1) == phi || cmpInst->get_operand(1) == upd) {
            ivIsLeft = false;
            bound = cmpInst->get_operand(0);
        } else {
            continue;
        }
        ivPhi = phi;
        break;
    }
    if (!ivPhi || !bound)
        return debugStructuredReject(func, loop, "no-iv");
    if (auto *boundInst = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(boundInst->parent_))
            return debugStructuredReject(func, loop, "variant-bound");

    int bodyInstCount = 0;
    int condBranchBlocks = 0;
    for (auto *bb : loop.blocks) {
        if (bb != header) {
            for (auto *inst : bb->instr_list_) {
                if (!inst->is_phi())
                    break;
                return debugStructuredReject(func, loop, "non-header-phi");
            }
        }
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (inst->is_call() || inst->is_alloca())
                return debugStructuredReject(func, loop, "unsupported-inst");
            bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                            dynamic_cast<UnaryInst *>(inst) ||
                            dynamic_cast<ICmpInst *>(inst) ||
                            dynamic_cast<FCmpInst *>(inst) ||
                            dynamic_cast<SelectInst *>(inst) ||
                            dynamic_cast<GetElementPtrInst *>(inst) ||
                            dynamic_cast<LoadInst *>(inst) ||
                            dynamic_cast<StoreInst *>(inst) ||
                            dynamic_cast<ZextInst *>(inst) ||
                            dynamic_cast<FpToSiInst *>(inst) ||
                            dynamic_cast<SiToFpInst *>(inst) ||
                            dynamic_cast<Bitcast *>(inst);
            if (!canClone)
                return debugStructuredReject(func, loop, "unsupported-inst");
            ++bodyInstCount;
        }
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (term && term->num_ops_ == 3 && bb != latch)
            ++condBranchBlocks;
    }
    if (bodyInstCount > MAX_STRUCTURED_LOOP_INSTS)
        return debugStructuredReject(func, loop, "too-many-body-insts");
    if (condBranchBlocks > 1)
        return debugStructuredReject(func, loop, "too-many-branches");

    int N = 0;
    if (loop.blocks.size() <= 3 && bodyInstCount <= 12) {
        N = 4;
    } else if (loop.blocks.size() <= 4 && bodyInstCount <= 16) {
        N = 2;
    } else {
        return debugStructuredReject(func, loop, "profitability");
    }
    if (bodyInstCount * N > 32)
        return debugStructuredReject(func, loop, "clone-budget");

    std::vector<UnrolledModuloRecurrence> modularRecurrences;
    std::unordered_map<PhiInst *, std::size_t> modularRecurrenceIndex;
    std::set<Instruction *> modularUpdateInstructions;
    if (N > 2) {
        for (auto *phi : headerPhis) {
            if (phi == ivPhi || phi->type_->tid_ != Type::IntegerTyID)
                continue;
            auto *remainder =
                dynamic_cast<BinaryInst *>(latchVals[phi]);
            UnrolledModuloRecurrence candidate;
            if (!remainder ||
                !prepareModuloRecurrence(
                    phi, remainder, initVals[phi], loop.blocks,
                    headerPhis, ivPhi, N, false, candidate) ||
                !isMustExecuteModuloRecurrence(
                    candidate, loop, func, latch))
                continue;
            modularRecurrenceIndex[phi] = modularRecurrences.size();
            modularUpdateInstructions.insert(
                candidate.updateChain.begin(), candidate.updateChain.end());
            modularRecurrences.push_back(std::move(candidate));
            if (std::getenv("DEBUG_LOOP_UNROLL"))
                std::cerr << "[LoopUnroll] func=" << func->name_
                          << " header=" << header->name_
                          << " modular-prefix=" << N - 1
                          << " form=structured\n";
        }
    }

    int guardAdj = 0;
    if (!computeBoundAdjustment(N - 1, strideVal, guardAdj))
        return debugStructuredReject(func, loop, "bound-adjustment-overflow");
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (strideVal > 0 && cb->value_ < guardAdj)
            return debugStructuredReject(func, loop, "bound-underflow");
    }

    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - guardAdj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, guardAdj);
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, adjC, preheader, true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    auto *headerMain = new BasicBlock(module, "unroll_main_hdr", func);
    std::unordered_map<PhiInst *, PhiInst *> mainPhis;
    for (int i = (int)headerPhis.size() - 1; i >= 0; --i) {
        auto *phi = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(initVals[phi], preheader);
        mainPhis[phi] = mainPhi;
    }

    ICmpInst *cmpMain = ivIsLeft
                            ? new ICmpInst(pred, mainPhis[ivPhi], boundMain, headerMain)
                            : new ICmpInst(pred, boundMain, mainPhis[ivPhi], headerMain);
    Value *mainCondition = cmpMain;
    if (!dynamic_cast<ConstantInt *>(bound))
        mainCondition = guardCondition(
            cmpMain,
            buildBoundAdjustmentGuard(bound, guardAdj, module, headerMain),
            module, headerMain);
    auto *remCheck = new BasicBlock(module, "unroll_rem_guard", func);
    ICmpInst *cmpRem = ivIsLeft
                           ? new ICmpInst(pred, mainPhis[ivPhi], bound, remCheck)
                           : new ICmpInst(pred, bound, mainPhis[ivPhi], remCheck);

    std::vector<std::unordered_map<BasicBlock *, BasicBlock *>> iterBBMaps(N);
    for (int iter = 0; iter < N; ++iter) {
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = new BasicBlock(module,
                                         "unroll_" + std::to_string(iter) + "_" + oldBB->name_,
                                         func);
            iterBBMaps[iter][oldBB] = newBB;
        }
    }

    auto headerCloneFor = [&](int iter) { return iterBBMaps[iter][header]; };

    std::unordered_map<PhiInst *, Value *> currentPhiVals;
    for (auto *phi : headerPhis)
        currentPhiVals[phi] = mainPhis[phi];

    for (int iter = 0; iter < N; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        for (auto *phi : headerPhis)
            valueMap[phi] = currentPhiVals[phi];

        auto &bbMap = iterBBMaps[iter];
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (oldInst->is_phi())
                    continue;
                if (oldInst->isTerminator()) {
                    auto *oldBr = dynamic_cast<BranchInst *>(oldInst);
                    if (!oldBr)
                        return false;
                    if (oldBB == latch) {
                        if (iter < N - 1) {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *accumulator =
                                    currentPhiVals[recurrence.state];
                                for (const auto &term :
                                     recurrence.contributionTerms) {
                                    Value *contribution = mapLoopValue(
                                        term.value, valueMap, bbMap);
                                    if (!contribution)
                                        return debugStructuredReject(
                                            func, loop,
                                            "modular-term-map-fail");
                                    accumulator = new BinaryInst(
                                        module->int32_ty_,
                                        term.sign > 0 ? Instruction::Add
                                                      : Instruction::Sub,
                                        accumulator, contribution, newBB);
                                }
                                if (iter == N - 2)
                                    accumulator = buildBoundedModulo(
                                        accumulator,
                                        recurrence.prefixLower,
                                        recurrence.prefixUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                                currentPhiVals[recurrence.state] = accumulator;
                            }
                        } else {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *mapped = mapLoopValue(
                                    recurrence.remainder, valueMap, bbMap);
                                auto *finalRemainder =
                                    dynamic_cast<BinaryInst *>(mapped);
                                if (!finalRemainder ||
                                    finalRemainder->op_id_ !=
                                        Instruction::SRem)
                                    return debugStructuredReject(
                                        func, loop,
                                        "modular-final-map-fail");
                                valueMap[recurrence.remainder] =
                                    buildBoundedModulo(
                                        finalRemainder->get_operand(0),
                                        recurrence.finalLower,
                                        recurrence.finalUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                            }
                        }
                        if (iter + 1 < N)
                            new BranchInst(headerCloneFor(iter + 1), newBB);
                        else
                            new BranchInst(headerMain, newBB);
                        continue;
                    }
                    if (oldBr->num_ops_ == 1) {
                        auto *dest = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(0), valueMap, bbMap));
                        if (!dest) return false;
                        new BranchInst(dest, newBB);
                    } else {
                        auto *cond = mapLoopValue(oldBr->get_operand(0), valueMap, bbMap);
                        auto *ifTrue = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(1), valueMap, bbMap));
                        auto *ifFalse = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(2), valueMap, bbMap));
                        if (!cond || !ifTrue || !ifFalse) return false;
                        new BranchInst(cond, ifTrue, ifFalse, newBB);
                    }
                    continue;
                }

                if (iter < N - 1 &&
                    modularUpdateInstructions.count(oldInst))
                    continue;

                auto *newInst = cloneInst(oldInst, newBB, valueMap);
                if (!newInst)
                    return false;
                valueMap[oldInst] = newInst;
            }
        }

        for (auto *phi : headerPhis) {
            if (iter < N - 1 && modularRecurrenceIndex.count(phi))
                continue;
            auto *mapped = mapLoopValue(latchVals[phi], valueMap, bbMap);
            if (!mapped)
                return debugStructuredReject(func, loop, "latch-map-fail");
            currentPhiVals[phi] = mapped;
        }
    }

    for (auto *phi : headerPhis)
        mainPhis[phi]->addIncoming(currentPhiVals[phi], iterBBMaps[N - 1][latch]);

    new BranchInst(mainCondition, headerCloneFor(0), remCheck, headerMain);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    auto *preBr = preheader->get_terminator();
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header)
            preBr->set_operand(i, headerMain);
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    for (auto *phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i, mainPhis[phi]);
                phi->set_operand(i + 1, remCheck);
                break;
            }
        }
    }

    std::unordered_map<Value *, Value *> remapToState;
    for (auto *phi : headerPhis) {
        remapToState[phi] = mainPhis[phi];
        remapToState[latchVals[phi]] = mainPhis[phi];
    }
    for (auto *inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == latch) {
                fromLatch = phi->get_operand(i);
                break;
            }
        }
        if (!fromLatch) continue;
        auto it = remapToState.find(fromLatch);
        if (it == remapToState.end()) {
            if (dynamic_cast<Constant *>(fromLatch))
                phi->addIncoming(fromLatch, remCheck);
            else
                return false;
        } else {
            phi->addIncoming(it->second, remCheck);
        }
    }

    return true;
}

bool LoopUnroll::tryUnrollStatefulWhileCFGRegion(Loop &loop, Function *func,
                                                 Module *module) {
    if (loop.blocks.size() <= 2)
        return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exitBB = loop.singleExit();
    if (!header || !latch || !preheader || !exitBB)
        return debugCFGRegionReject(func, loop, "stateful-missing-structural-block");
    if (loop.exiting.size() != 1 || loop.exiting[0] != header)
        return debugCFGRegionReject(func, loop, "stateful-non-header-exit");

    auto *headerBr = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerBr || headerBr->num_ops_ != 3)
        return debugCFGRegionReject(func, loop, "stateful-header-not-cond-branch");
    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerBr->get_operand(1));
    auto *headerExit = dynamic_cast<BasicBlock *>(headerBr->get_operand(2));
    if (!bodyEntry || !headerExit || !loop.blocks.count(bodyEntry) ||
        headerExit != exitBB)
        return debugCFGRegionReject(func, loop, "stateful-header-successors");

    auto *latchBr = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchBr || latchBr->num_ops_ != 1 || latchBr->get_operand(0) != header)
        return debugCFGRegionReject(func, loop, "stateful-latch-not-backedge");

    std::vector<PhiInst *> headerPhis;
    std::unordered_map<PhiInst *, Value *> initVals;
    std::unordered_map<PhiInst *, Value *> latchVals;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4)
            return debugCFGRegionReject(func, loop, "stateful-non-canonical-header-phi");
        Value *init = nullptr;
        Value *back = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == preheader)
                init = phi->get_operand(i);
            else if (src == latch)
                back = phi->get_operand(i);
            else
                return debugCFGRegionReject(func, loop, "stateful-header-phi-edge");
        }
        if (!init || !back)
            return debugCFGRegionReject(func, loop, "stateful-header-phi-missing-edge");
        headerPhis.push_back(phi);
        initVals[phi] = init;
        latchVals[phi] = back;
    }
    if (headerPhis.empty())
        return debugCFGRegionReject(func, loop, "stateful-no-header-phis");

    auto *cmpInst = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
    if (!cmpInst || cmpInst->icmp_op_ != ICmpInst::ICMP_SLT)
        return debugCFGRegionReject(func, loop, "stateful-unsupported-predicate");

    PhiInst *ivPhi = nullptr;
    Value *bound = nullptr;
    for (auto *phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *upd = dynamic_cast<Instruction *>(latchVals[phi]);
        if (!upd || upd->parent_ != latch || !upd->is_add())
            continue;
        auto *c0 = dynamic_cast<ConstantInt *>(upd->get_operand(0));
        auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
        bool stepOne = (upd->get_operand(0) == phi && c1 && c1->value_ == 1) ||
                       (upd->get_operand(1) == phi && c0 && c0->value_ == 1);
        if (!stepOne)
            continue;
        if (cmpInst->get_operand(0) != phi)
            continue;
        ivPhi = phi;
        bound = cmpInst->get_operand(1);
        break;
    }
    if (!ivPhi || !bound)
        return debugCFGRegionReject(func, loop, "stateful-no-iv");
    if (auto *boundInst = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(boundInst->parent_))
            return debugCFGRegionReject(func, loop, "stateful-variant-bound");

    bool hasState = false;
    for (auto *phi : headerPhis) {
        if (phi != ivPhi) {
            hasState = true;
            break;
        }
    }
    if (!hasState)
        return debugCFGRegionReject(func, loop, "stateful-no-carried-state");

    int bodyInstCount = 0;
    int condBranchBlocks = 0;
    int memoryOps = 0;
    int vectorOps = 0;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi())
                continue;
            if (inst->isTerminator()) {
                auto *br = dynamic_cast<BranchInst *>(inst);
                if (!br)
                    return debugCFGRegionReject(func, loop, "stateful-non-branch-terminator");
                for (unsigned i = br->num_ops_ == 3 ? 1 : 0; i < br->num_ops_; ++i) {
                    auto *succ = dynamic_cast<BasicBlock *>(br->get_operand(i));
                    if (!succ)
                        return debugCFGRegionReject(func, loop, "stateful-bad-branch-target");
                    if (bb == header && succ == exitBB)
                        continue;
                    if (!loop.blocks.count(succ))
                        return debugCFGRegionReject(func, loop, "stateful-branch-exits-region");
                }
                if (br->num_ops_ == 3)
                    ++condBranchBlocks;
                continue;
            }
            if (inst->is_call() || inst->is_alloca())
                return debugCFGRegionReject(func, loop, "stateful-side-effect");
            if (!isCloneableForUnroll(inst))
                return debugCFGRegionReject(func, loop, "stateful-unsupported-inst");
            if (inst->is_store() && dependsOnAlloca(inst->get_operand(1)))
                return debugCFGRegionReject(func, loop, "stateful-stack-store");
            if (inst->is_load() || inst->is_store() || inst->is_gep())
                ++memoryOps;
            if (inst->type_->tid_ == Type::VectorTyID)
                ++vectorOps;
            for (unsigned i = 0; i < inst->num_ops_; ++i)
                if (inst->get_operand(i)->type_->tid_ == Type::VectorTyID)
                    ++vectorOps;
            ++bodyInstCount;
        }
    }
    if (bodyInstCount > 72 || loop.blocksOrdered.size() > 18)
        return debugCFGRegionReject(func, loop, "stateful-clone-budget");
    if (!isProfitableCFGRegionUnroll(loop, bodyInstCount, condBranchBlocks,
                                     memoryOps, vectorOps))
        return debugCFGRegionReject(func, loop, "stateful-profitability");

    struct OutsideUse { Instruction *user; unsigned idx; };
    std::map<Value *, std::vector<OutsideUse>> liveOuts;
    auto collectLiveOut = [&](Instruction *inst) {
        for (const auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || loop.blocks.count(user->parent_))
                continue;
            if (user->parent_ == exitBB && user->is_phi())
                continue;
            liveOuts[inst].push_back({user, use.arg_no_});
        }
    };
    for (auto *phi : headerPhis)
        collectLiveOut(phi);
    for (auto *bb : loop.blocksOrdered) {
        if (bb == header) continue;
        for (auto *inst : bb->instr_list_) {
            if (inst->isTerminator()) continue;
            collectLiveOut(inst);
        }
    }
    for (auto &entry : liveOuts) {
        if (std::find(headerPhis.begin(), headerPhis.end(), entry.first) ==
            headerPhis.end())
            return debugCFGRegionReject(func, loop, "stateful-non-header-liveout");
    }

    int N = 2;
    std::vector<UnrolledModuloRecurrence> modularRecurrences;
    std::unordered_map<PhiInst *, std::size_t> modularRecurrenceIndex;
    std::set<Instruction *> modularUpdateInstructions;
    if (bodyInstCount <= 24 && loop.blocksOrdered.size() <= 12) {
        constexpr int modularFactor = 4;
        for (auto *phi : headerPhis) {
            if (phi == ivPhi || phi->type_->tid_ != Type::IntegerTyID)
                continue;
            auto *remainder =
                dynamic_cast<BinaryInst *>(latchVals[phi]);
            UnrolledModuloRecurrence candidate;
            if (!remainder ||
                !prepareModuloRecurrence(
                    phi, remainder, initVals[phi], loop.blocks,
                    headerPhis, ivPhi, modularFactor, false, candidate) ||
                !isMustExecuteModuloRecurrence(
                    candidate, loop, func, latch))
                continue;
            modularRecurrenceIndex[phi] = modularRecurrences.size();
            modularUpdateInstructions.insert(
                candidate.updateChain.begin(), candidate.updateChain.end());
            modularRecurrences.push_back(std::move(candidate));
        }
        if (!modularRecurrences.empty()) {
            N = modularFactor;
            if (std::getenv("DEBUG_LOOP_UNROLL"))
                std::cerr << "[LoopUnroll] func=" << func->name_
                          << " header=" << header->name_
                          << " modular-prefix=" << N - 1
                          << " form=stateful-while-cfg\n";
        }
    }
    const int guardAdj = N - 1;
    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (cb->value_ < guardAdj)
            return debugCFGRegionReject(func, loop, "stateful-bound-underflow");
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - guardAdj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, guardAdj);
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, adjC, preheader, true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << N << " form=stateful-while-cfg\n";

    auto *headerMain = new BasicBlock(module, "unroll_state_hdr", func);
    std::unordered_map<PhiInst *, PhiInst *> mainPhis;
    for (int i = (int)headerPhis.size() - 1; i >= 0; --i) {
        auto *phi = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(initVals[phi], preheader);
        mainPhis[phi] = mainPhi;
    }

    auto *cmpMain = new ICmpInst(ICmpInst::ICMP_SLT, mainPhis[ivPhi],
                                 boundMain, headerMain);
    Value *mainCondition = cmpMain;
    if (!dynamic_cast<ConstantInt *>(bound))
        mainCondition = guardCondition(
            cmpMain,
            buildBoundAdjustmentGuard(bound, guardAdj, module, headerMain),
            module, headerMain);
    auto *remCheck = new BasicBlock(module, "unroll_state_rem", func);
    auto *cmpRem = new ICmpInst(ICmpInst::ICMP_SLT, mainPhis[ivPhi],
                                bound, remCheck);

    std::vector<std::unordered_map<BasicBlock *, BasicBlock *>> iterBBMaps(N);
    for (int iter = 0; iter < N; ++iter) {
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = new BasicBlock(module,
                                         "unroll_state_" + std::to_string(iter) +
                                             "_" + oldBB->name_,
                                         func);
            iterBBMaps[iter][oldBB] = newBB;
        }
    }

    auto headerCloneFor = [&](int iter) { return iterBBMaps[iter][header]; };

    std::unordered_map<PhiInst *, Value *> currentPhiVals;
    for (auto *phi : headerPhis)
        currentPhiVals[phi] = mainPhis[phi];

    for (int iter = 0; iter < N; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        auto &bbMap = iterBBMaps[iter];

        for (auto *phi : headerPhis)
            valueMap[phi] = currentPhiVals[phi];

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = PhiInst::create_phi(oldInst->type_, newBB);
                newBB->add_instruction(newPhi);
                valueMap[oldInst] = newPhi;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = bbMap[oldBB];
            if (oldBB == header) {
                new BranchInst(bbMap[bodyEntry], newBB);
                continue;
            }

            for (auto *oldInst : oldBB->instr_list_) {
                if (oldInst->is_phi())
                    continue;
                if (oldInst->isTerminator()) {
                    auto *oldBr = dynamic_cast<BranchInst *>(oldInst);
                    if (!oldBr)
                        return false;
                    if (oldBB == latch) {
                        if (iter < N - 1) {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *accumulator =
                                    currentPhiVals[recurrence.state];
                                for (const auto &term :
                                     recurrence.contributionTerms) {
                                    Value *contribution = mapLoopValue(
                                        term.value, valueMap, bbMap);
                                    if (!contribution)
                                        return debugCFGRegionReject(
                                            func, loop,
                                            "stateful-modular-term-map-fail");
                                    accumulator = new BinaryInst(
                                        module->int32_ty_,
                                        term.sign > 0 ? Instruction::Add
                                                      : Instruction::Sub,
                                        accumulator, contribution, newBB);
                                }
                                if (iter == N - 2)
                                    accumulator = buildBoundedModulo(
                                        accumulator,
                                        recurrence.prefixLower,
                                        recurrence.prefixUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                                currentPhiVals[recurrence.state] = accumulator;
                            }
                        } else {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *mapped = mapLoopValue(
                                    recurrence.remainder, valueMap, bbMap);
                                auto *finalRemainder =
                                    dynamic_cast<BinaryInst *>(mapped);
                                if (!finalRemainder ||
                                    finalRemainder->op_id_ !=
                                        Instruction::SRem)
                                    return debugCFGRegionReject(
                                        func, loop,
                                        "stateful-modular-final-map-fail");
                                valueMap[recurrence.remainder] =
                                    buildBoundedModulo(
                                        finalRemainder->get_operand(0),
                                        recurrence.finalLower,
                                        recurrence.finalUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                            }
                        }
                        if (iter + 1 < N)
                            new BranchInst(headerCloneFor(iter + 1), newBB);
                        else
                            new BranchInst(headerMain, newBB);
                        continue;
                    }
                    if (oldBr->num_ops_ == 1) {
                        auto *dest = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(0), valueMap, bbMap));
                        if (!dest) return false;
                        new BranchInst(dest, newBB);
                    } else {
                        auto *cond = mapLoopValue(oldBr->get_operand(0), valueMap, bbMap);
                        auto *ifTrue = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(1), valueMap, bbMap));
                        auto *ifFalse = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(2), valueMap, bbMap));
                        if (!cond || !ifTrue || !ifFalse) return false;
                        new BranchInst(cond, ifTrue, ifFalse, newBB);
                    }
                    continue;
                }

                if (iter < N - 1 &&
                    modularUpdateInstructions.count(oldInst))
                    continue;

                auto *newInst = cloneInst(oldInst, newBB, valueMap);
                if (!newInst)
                    return false;
                valueMap[oldInst] = newInst;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = static_cast<PhiInst *>(valueMap[oldInst]);
                for (unsigned i = 0; i < oldInst->num_ops_; i += 2) {
                    auto *oldPred = dynamic_cast<BasicBlock *>(oldInst->get_operand(i + 1));
                    if (!oldPred || !loop.blocks.count(oldPred))
                        return debugCFGRegionReject(func, loop, "stateful-internal-phi-outside-pred");
                    Value *mappedVal = mapLoopValue(oldInst->get_operand(i), valueMap, bbMap);
                    auto *mappedPred = dynamic_cast<BasicBlock *>(
                        mapLoopValue(oldPred, valueMap, bbMap));
                    if (!mappedVal || !mappedPred)
                        return debugCFGRegionReject(func, loop, "stateful-internal-phi-map-fail");
                    newPhi->addIncoming(mappedVal, mappedPred);
                }
            }
        }

        for (auto *phi : headerPhis) {
            if (iter < N - 1 && modularRecurrenceIndex.count(phi))
                continue;
            Value *mapped = mapLoopValue(latchVals[phi], valueMap, bbMap);
            if (!mapped)
                return debugCFGRegionReject(func, loop, "stateful-latch-map-fail");
            currentPhiVals[phi] = mapped;
        }
    }

    for (auto *phi : headerPhis)
        mainPhis[phi]->addIncoming(currentPhiVals[phi], iterBBMaps[N - 1][latch]);

    new BranchInst(mainCondition, headerCloneFor(0), remCheck, headerMain);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    auto *preBr = preheader->get_terminator();
    bool redirected = false;
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header) {
            preBr->set_operand(i, headerMain);
            redirected = true;
        }
    }
    if (!redirected)
        return debugCFGRegionReject(func, loop, "stateful-preheader-no-edge");
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    for (auto *phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i, mainPhis[phi]);
                phi->set_operand(i + 1, remCheck);
                break;
            }
        }
    }

    auto mapMainState = [&](Value *v) -> Value * {
        if (auto *phi = dynamic_cast<PhiInst *>(v)) {
            auto it = mainPhis.find(phi);
            if (it != mainPhis.end())
                return it->second;
        }
        for (auto *phi : headerPhis)
            if (latchVals[phi] == v)
                return mainPhis[phi];
        return v;
    };

    for (auto *inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromHeader = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == header) {
                fromHeader = phi->get_operand(i);
                break;
            }
        }
        if (fromHeader)
            phi->addIncoming(mapMainState(fromHeader), remCheck);
    }

    for (auto *phi : headerPhis) {
        auto liveOutIt = liveOuts.find(phi);
        if (liveOutIt == liveOuts.end())
            continue;
        auto *mergePhi = PhiInst::create_phi(phi->type_, exitBB);
        exitBB->add_instruction_front(mergePhi);
        mergePhi->addIncoming(phi, header);
        mergePhi->addIncoming(mainPhis[phi], remCheck);
        for (auto &use : liveOutIt->second)
            use.user->set_operand(use.idx, mergePhi);
    }

    return true;
}

bool LoopUnroll::tryUnrollCFGRegion(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() <= 4)
        return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    BasicBlock *exitBB = loop.singleExit();
    if (!header || !latch || !preheader || !exitBB)
        return debugCFGRegionReject(func, loop, "missing-structural-block");
    if (loop.exiting.size() != 1 || loop.exiting[0] != latch)
        return debugCFGRegionReject(func, loop, "non-latch-exit");

    auto *latchBr = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchBr || latchBr->num_ops_ != 3)
        return debugCFGRegionReject(func, loop, "latch-not-cond-branch");
    auto *latchTrue = dynamic_cast<BasicBlock *>(latchBr->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchBr->get_operand(2));
    if (latchTrue != header || latchFalse != exitBB)
        return debugCFGRegionReject(func, loop, "unsupported-latch-branch");

    std::vector<PhiInst *> headerPhis;
    std::unordered_map<PhiInst *, Value *> initVals;
    std::unordered_map<PhiInst *, Value *> latchVals;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ != 4)
            return debugCFGRegionReject(func, loop, "non-canonical-header-phi");
        Value *init = nullptr;
        Value *back = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == preheader)
                init = phi->get_operand(i);
            else if (src == latch)
                back = phi->get_operand(i);
            else
                return debugCFGRegionReject(func, loop, "header-phi-edge");
        }
        if (!init || !back)
            return debugCFGRegionReject(func, loop, "header-phi-missing-edge");
        headerPhis.push_back(phi);
        initVals[phi] = init;
        latchVals[phi] = back;
    }
    if (headerPhis.empty())
        return debugCFGRegionReject(func, loop, "no-header-phis");

    auto *cmpInst = dynamic_cast<ICmpInst *>(latchBr->get_operand(0));
    if (!cmpInst || cmpInst->icmp_op_ != ICmpInst::ICMP_SLT)
        return debugCFGRegionReject(func, loop, "unsupported-predicate");

    PhiInst *ivPhi = nullptr;
    Instruction *ivUpdate = nullptr;
    Value *bound = nullptr;
    for (auto *phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *upd = dynamic_cast<Instruction *>(latchVals[phi]);
        if (!upd || upd->parent_ != latch || !upd->is_add())
            continue;
        auto *c0 = dynamic_cast<ConstantInt *>(upd->get_operand(0));
        auto *c1 = dynamic_cast<ConstantInt *>(upd->get_operand(1));
        bool stepOne = (upd->get_operand(0) == phi && c1 && c1->value_ == 1) ||
                       (upd->get_operand(1) == phi && c0 && c0->value_ == 1);
        if (!stepOne)
            continue;
        if (cmpInst->get_operand(0) != phi)
            continue;
        bound = cmpInst->get_operand(1);
        ivPhi = phi;
        ivUpdate = upd;
        break;
    }
    if (!ivPhi || !ivUpdate || !bound)
        return debugCFGRegionReject(func, loop, "no-iv");
    if (auto *boundInst = dynamic_cast<Instruction *>(bound))
        if (loop.blocks.count(boundInst->parent_))
            return debugCFGRegionReject(func, loop, "variant-bound");

    auto *initConst = dynamic_cast<ConstantInt *>(initVals[ivPhi]);
    if (!initConst || initConst->value_ < 0)
        return debugCFGRegionReject(func, loop, "non-constant-nonnegative-init");
    if (!dynamic_cast<ConstantInt *>(bound) &&
        !hasEntryLowerBoundGuard(preheader, header, bound, initConst))
        return debugCFGRegionReject(func, loop, "missing-entry-bound-guard");

    int bodyInstCount = 0;
    int condBranchBlocks = 0;
    int memoryOps = 0;
    int vectorOps = 0;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (inst->is_call() || inst->is_store() || inst->is_alloca())
                return debugCFGRegionReject(func, loop, "side-effect");
            if (!isCloneableForUnroll(inst))
                return debugCFGRegionReject(func, loop, "unsupported-inst");
            if (inst->is_load() || inst->is_gep())
                ++memoryOps;
            if (inst->type_->tid_ == Type::VectorTyID)
                ++vectorOps;
            for (unsigned i = 0; i < inst->num_ops_; ++i)
                if (inst->get_operand(i)->type_->tid_ == Type::VectorTyID)
                    ++vectorOps;
            ++bodyInstCount;
        }
        auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (term && term->num_ops_ == 3)
            ++condBranchBlocks;
    }
    if (bodyInstCount > 48 || loop.blocksOrdered.size() > 16)
        return debugCFGRegionReject(func, loop, "clone-budget");
    if (!isProfitableCFGRegionUnroll(loop, bodyInstCount, condBranchBlocks,
                                     memoryOps, vectorOps))
        return debugCFGRegionReject(func, loop, "profitability");

    struct OutsideUse { Instruction *user; unsigned idx; };
    std::map<Value *, std::vector<OutsideUse>> liveOuts;
    for (auto *bb : loop.blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->isTerminator()) continue;
            for (const auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || !user->parent_ || loop.blocks.count(user->parent_))
                    continue;
                if (user->parent_ == exitBB && user->is_phi())
                    continue;
                liveOuts[inst].push_back({user, use.arg_no_});
            }
        }
    }

    ICmpInst *firstIterCmp = nullptr;
    if (auto *headerBr = dynamic_cast<BranchInst *>(header->get_terminator())) {
        if (headerBr->num_ops_ == 3) {
            auto *cmp = dynamic_cast<ICmpInst *>(headerBr->get_operand(0));
            if (cmp && cmp->icmp_op_ == ICmpInst::ICMP_EQ) {
                auto *c0 = dynamic_cast<ConstantInt *>(cmp->get_operand(0));
                auto *c1 = dynamic_cast<ConstantInt *>(cmp->get_operand(1));
                bool matchesInit =
                    (cmp->get_operand(0) == ivPhi && c1 && c1->value_ == initConst->value_) ||
                    (cmp->get_operand(1) == ivPhi && c0 && c0->value_ == initConst->value_);
                if (matchesInit)
                    firstIterCmp = cmp;
            }
        }
    }

    const int N = DEFAULT_UNROLL_FACTOR;
    const int guardAdj = N - 1;

    std::vector<UnrolledModuloRecurrence> modularRecurrences;
    std::unordered_map<PhiInst *, std::size_t> modularRecurrenceIndex;
    std::set<Instruction *> modularUpdateInstructions;
    for (auto *phi : headerPhis) {
        if (phi == ivPhi || phi->type_->tid_ != Type::IntegerTyID)
            continue;
        auto *remainder = dynamic_cast<BinaryInst *>(latchVals[phi]);
        UnrolledModuloRecurrence candidate;
        if (!remainder ||
            !prepareModuloRecurrence(
                phi, remainder, initVals[phi], loop.blocks,
                headerPhis, ivPhi, N, false, candidate) ||
            !isMustExecuteModuloRecurrence(candidate, loop, func, latch))
            continue;
        modularRecurrenceIndex[phi] = modularRecurrences.size();
        modularUpdateInstructions.insert(
            candidate.updateChain.begin(), candidate.updateChain.end());
        modularRecurrences.push_back(std::move(candidate));
        if (std::getenv("DEBUG_LOOP_UNROLL"))
            std::cerr << "[LoopUnroll] func=" << func->name_
                      << " header=" << header->name_
                      << " modular-prefix=" << N - 1
                      << " form=cfg-region\n";
    }

    Value *boundMain = nullptr;
    if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        if (cb->value_ < initConst->value_ + guardAdj)
            return debugCFGRegionReject(func, loop, "constant-trip-too-small");
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - guardAdj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, guardAdj);
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, adjC, preheader, true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << N << " form=cfg-region\n";

    auto *headerMain = new BasicBlock(module, "unroll_cfg_hdr", func);
    std::unordered_map<PhiInst *, PhiInst *> mainPhis;
    for (int i = (int)headerPhis.size() - 1; i >= 0; --i) {
        auto *phi = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, headerMain);
        headerMain->add_instruction_front(mainPhi);
        mainPhi->addIncoming(initVals[phi], preheader);
        mainPhis[phi] = mainPhi;
    }
    auto *cmpMain = new ICmpInst(ICmpInst::ICMP_SLE, mainPhis[ivPhi],
                                 boundMain, headerMain);
    Value *mainCondition = cmpMain;
    if (!dynamic_cast<ConstantInt *>(bound))
        mainCondition = guardCondition(
            cmpMain,
            buildBoundAdjustmentGuard(bound, guardAdj, module, headerMain),
            module, headerMain);

    auto *remCheck = new BasicBlock(module, "unroll_cfg_rem", func);
    auto *cmpRem = new ICmpInst(ICmpInst::ICMP_SLE, mainPhis[ivPhi],
                                bound, remCheck);

    std::vector<std::unordered_map<BasicBlock *, BasicBlock *>> iterBBMaps(N);
    for (int iter = 0; iter < N; ++iter) {
        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = new BasicBlock(module,
                                         "unroll_cfg_" + std::to_string(iter) +
                                             "_" + oldBB->name_,
                                         func);
            iterBBMaps[iter][oldBB] = newBB;
        }
    }

    auto headerCloneFor = [&](int iter) { return iterBBMaps[iter][header]; };

    std::unordered_map<PhiInst *, Value *> currentPhiVals;
    for (auto *phi : headerPhis)
        currentPhiVals[phi] = mainPhis[phi];

    std::unordered_map<Value *, Value *> finalValueMap;
    for (int iter = 0; iter < N; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        auto &bbMap = iterBBMaps[iter];

        for (auto *phi : headerPhis)
            valueMap[phi] = currentPhiVals[phi];

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = PhiInst::create_phi(oldInst->type_, newBB);
                newBB->add_instruction(newPhi);
                valueMap[oldInst] = newPhi;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (oldInst->is_phi())
                    continue;
                if (oldInst->isTerminator()) {
                    auto *oldBr = dynamic_cast<BranchInst *>(oldInst);
                    if (!oldBr)
                        return false;
                    if (oldBB == latch) {
                        if (iter < N - 1) {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *accumulator =
                                    currentPhiVals[recurrence.state];
                                for (const auto &term :
                                     recurrence.contributionTerms) {
                                    Value *contribution = mapLoopValue(
                                        term.value, valueMap, bbMap);
                                    if (!contribution)
                                        return debugCFGRegionReject(
                                            func, loop,
                                            "modular-term-map-fail");
                                    accumulator = new BinaryInst(
                                        module->int32_ty_,
                                        term.sign > 0 ? Instruction::Add
                                                      : Instruction::Sub,
                                        accumulator, contribution, newBB);
                                }
                                if (iter == N - 2)
                                    accumulator = buildBoundedModulo(
                                        accumulator,
                                        recurrence.prefixLower,
                                        recurrence.prefixUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                                currentPhiVals[recurrence.state] = accumulator;
                            }
                        } else {
                            for (const auto &recurrence :
                                 modularRecurrences) {
                                Value *mapped = mapLoopValue(
                                    recurrence.remainder, valueMap, bbMap);
                                auto *finalRemainder =
                                    dynamic_cast<BinaryInst *>(mapped);
                                if (!finalRemainder ||
                                    finalRemainder->op_id_ !=
                                        Instruction::SRem)
                                    return debugCFGRegionReject(
                                        func, loop,
                                        "modular-final-map-fail");
                                valueMap[recurrence.remainder] =
                                    buildBoundedModulo(
                                        finalRemainder->get_operand(0),
                                        recurrence.finalLower,
                                        recurrence.finalUpper,
                                        recurrence.modulus->value_, module,
                                        newBB);
                            }
                        }
                        if (iter + 1 < N)
                            new BranchInst(headerCloneFor(iter + 1), newBB);
                        else
                            new BranchInst(headerMain, newBB);
                        continue;
                    }
                    if (oldBr->num_ops_ == 1) {
                        auto *dest = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(0), valueMap, bbMap));
                        if (!dest) return false;
                        new BranchInst(dest, newBB);
                    } else {
                        auto *cond = mapLoopValue(oldBr->get_operand(0), valueMap, bbMap);
                        auto *ifTrue = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(1), valueMap, bbMap));
                        auto *ifFalse = dynamic_cast<BasicBlock *>(
                            mapLoopValue(oldBr->get_operand(2), valueMap, bbMap));
                        if (!cond || !ifTrue || !ifFalse) return false;
                        new BranchInst(cond, ifTrue, ifFalse, newBB);
                    }
                    continue;
                }
                if (oldInst == firstIterCmp && iter > 0) {
                    valueMap[oldInst] = new ConstantInt(module->int1_ty_, 0);
                    continue;
                }
                if (iter < N - 1 &&
                    modularUpdateInstructions.count(oldInst))
                    continue;
                auto *newInst = cloneInst(oldInst, newBB, valueMap);
                if (!newInst)
                    return false;
                valueMap[oldInst] = newInst;
            }
        }

        for (auto *oldBB : loop.blocksOrdered) {
            if (oldBB == header)
                continue;
            auto *newBB = bbMap[oldBB];
            for (auto *oldInst : oldBB->instr_list_) {
                if (!oldInst->is_phi()) break;
                auto *newPhi = static_cast<PhiInst *>(valueMap[oldInst]);
                for (unsigned i = 0; i < oldInst->num_ops_; i += 2) {
                    auto *oldPred = dynamic_cast<BasicBlock *>(oldInst->get_operand(i + 1));
                    if (!oldPred || !loop.blocks.count(oldPred))
                        return debugCFGRegionReject(func, loop, "internal-phi-outside-pred");
                    Value *mappedVal = mapLoopValue(oldInst->get_operand(i), valueMap, bbMap);
                    auto *mappedPred = dynamic_cast<BasicBlock *>(
                        mapLoopValue(oldPred, valueMap, bbMap));
                    if (!mappedVal || !mappedPred)
                        return debugCFGRegionReject(func, loop, "internal-phi-map-fail");
                    newPhi->addIncoming(mappedVal, mappedPred);
                }
            }
        }

        for (auto *phi : headerPhis) {
            if (iter < N - 1 && modularRecurrenceIndex.count(phi))
                continue;
            Value *mapped = mapLoopValue(latchVals[phi], valueMap, bbMap);
            if (!mapped)
                return debugCFGRegionReject(func, loop, "latch-map-fail");
            currentPhiVals[phi] = mapped;
        }
        if (iter == N - 1)
            finalValueMap = std::move(valueMap);
    }

    for (auto *phi : headerPhis)
        mainPhis[phi]->addIncoming(currentPhiVals[phi], iterBBMaps[N - 1][latch]);

    new BranchInst(mainCondition, headerCloneFor(0), remCheck, headerMain);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    auto *preBr = preheader->get_terminator();
    bool redirected = false;
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header) {
            preBr->set_operand(i, headerMain);
            redirected = true;
        }
    }
    if (!redirected)
        return debugCFGRegionReject(func, loop, "preheader-no-edge");
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(headerMain);
    header->remove_pre_basic_block(preheader);
    headerMain->add_pre_basic_block(preheader);

    for (auto *phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i, mainPhis[phi]);
                phi->set_operand(i + 1, remCheck);
                break;
            }
        }
    }

    auto mapFinal = [&](Value *v) -> Value * {
        if (auto *phi = dynamic_cast<PhiInst *>(v)) {
            auto it = currentPhiVals.find(phi);
            if (it != currentPhiVals.end())
                return it->second;
        }
        for (auto *phi : headerPhis)
            if (latchVals[phi] == v)
                return currentPhiVals[phi];
        auto it = finalValueMap.find(v);
        return it != finalValueMap.end() ? it->second : v;
    };

    auto carriedModuloFor = [&](Value *raw, PhiInst *exitPhi) -> Value * {
        auto phiFeedsOnlySameSRem = [&](ConstantInt *mod) {
            bool sawUse = false;
            for (const auto &use : exitPhi->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || user->parent_ != exitBB || !user->is_rem())
                    return false;
                auto *userMod = dynamic_cast<ConstantInt *>(user->get_operand(1));
                if (!userMod || userMod->value_ != mod->value_)
                    return false;
                sawUse = true;
            }
            return sawUse;
        };

        for (auto *phi : headerPhis) {
            auto *rem = dynamic_cast<Instruction *>(latchVals[phi]);
            if (!rem || rem->parent_ != latch || !rem->is_rem())
                continue;
            if (rem->get_operand(0) != raw)
                continue;
            auto *mod = dynamic_cast<ConstantInt *>(rem->get_operand(1));
            if (!mod || !phiFeedsOnlySameSRem(mod))
                continue;
            return currentPhiVals[phi];
        }
        return nullptr;
    };

    for (auto *inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == latch)
                fromLatch = phi->get_operand(i);
        if (fromLatch) {
            Value *finalVal = carriedModuloFor(fromLatch, phi);
            phi->addIncoming(finalVal ? finalVal : mapFinal(fromLatch), remCheck);
        }
    }

    // `liveOuts` is keyed by pointer for lookup only.  Iterating that map would
    // make the order of the newly prepended phis depend on process addresses.
    // Revisit the loop in canonical IR order when materializing those phis.
    for (auto *bb : loop.blocksOrdered) {
        for (auto *val : bb->instr_list_) {
            auto liveOutIt = liveOuts.find(val);
            if (liveOutIt == liveOuts.end()) continue;
            auto &uses = liveOutIt->second;
            auto *mergePhi = PhiInst::create_phi(val->type_, exitBB);
            exitBB->add_instruction_front(mergePhi);
            mergePhi->addIncoming(val, latch);
            mergePhi->addIncoming(mapFinal(val), remCheck);
            for (auto &use : uses)
                use.user->set_operand(use.idx, mergePhi);
        }
    }

    return true;
}

// ── Do-while (rotated single-block) unrolling ─────────────────────────────
//
// 形态：loop = { B }，B: phis…, body…, ivUpdate, cmp(ivUpdate, bound),
//       br cmp, B, exit。check 在 body 之后 ⇒ 展开需要：
//   checkBlock:  iv_init <op> bound-adj ? mainLoop : B（不足 N 次直接走原循环）
//   mainLoop:    N 份 body 链式克隆，cmp(iv_N, bound-adj) 回边/出
//   remCheck:    cmp(iv_N, bound) ? B（余数 1..N-1 次）: exit（余数 0 次）
//   B:           phi 入边改为 [init, checkBlock], [iv_N 等, remCheck]
//   exit:        来自 B 的 live-out 补 [主循环末值, remCheck] 入边
bool LoopUnroll::tryUnrollDoWhile(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() != 1) return false;

    BasicBlock *header = loop.header;
    if (loop.singleLatch() != header) return false;
    BasicBlock *preheader = loop.preheader;
    if (!preheader) return false;

    auto *term = header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;
    auto *trueSucc  = dynamic_cast<BasicBlock *>(term->get_operand(1));
    auto *falseSucc = dynamic_cast<BasicBlock *>(term->get_operand(2));
    if (trueSucc != header || !falseSucc || falseSucc == header) return false;
    BasicBlock *exitBB = falseSucc;
    // 需要专用出口（exit 唯一前驱是 header），否则 live-out phi 修补
    // 无法只看本循环
    if (exitBB->pre_bbs_.size() != 1 || exitBB->pre_bbs_[0] != header)
        return false;

    // ── 收集 phi ────────────────────────────────────────────────────────
    std::vector<PhiInst *> headerPhis;
    for (auto inst : header->instr_list_) {
        if (!inst->is_phi()) break;
        headerPhis.push_back(static_cast<PhiInst *>(inst));
    }
    if (headerPhis.empty()) return false;

    auto getInitVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == preheader)
                return phi->get_operand(i);
        return nullptr;
    };
    auto getBackVal = [&](PhiInst *phi) -> Value * {
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == header)
                return phi->get_operand(i);
        return nullptr;
    };
    for (auto phi : headerPhis)
        if (phi->num_ops_ != 4 || !getInitVal(phi) || !getBackVal(phi))
            return false;

    // ── 识别 IV：backedge 入值 = add/sub(phi, 常量)，或
    //    add(phi, loop-invariant step) ──────────────────────────────────
    PhiInst     *ivPhi    = nullptr;
    Instruction *ivUpdate = nullptr;
    int          strideVal = 0;
    Value       *strideValue = nullptr;
    bool         dynamicStride = false;
    for (auto phi : headerPhis) {
        if (phi->type_->tid_ != Type::IntegerTyID) continue;
        auto *upd = dynamic_cast<Instruction *>(getBackVal(phi));
        if (!upd || upd->parent_ != header) continue;
        if (!upd->is_add() && !upd->is_sub()) continue;
        Value *op0 = upd->get_operand(0);
        Value *op1 = upd->get_operand(1);
        if (upd->is_add()) {
            auto *c0 = dynamic_cast<ConstantInt *>(op0);
            auto *c1 = dynamic_cast<ConstantInt *>(op1);
            ConstantInt *c = c1 ? c1 : c0;
            Value *base = c1 ? op0 : op1;
            if (c && c->value_ > 0 && base == phi) {
                strideVal = c->value_;
            } else {
                Value *candidate = nullptr;
                if (op0 == phi)
                    candidate = op1;
                else if (op1 == phi)
                    candidate = op0;
                auto *candidateInst =
                    dynamic_cast<Instruction *>(candidate);
                auto *candidateTy =
                    candidate
                        ? dynamic_cast<IntegerType *>(candidate->type_)
                        : nullptr;
                if (!candidate || !candidateTy ||
                    candidateTy->num_bits_ != 32 ||
                    (candidateInst && candidateInst->parent_ == header))
                    continue;
                strideValue = candidate;
                dynamicStride = true;
            }
        } else {
            auto *c1 = dynamic_cast<ConstantInt *>(op1);
            if (!c1 || c1->value_ <= 0 || op0 != phi) continue;
            strideVal = -c1->value_;
        }
        ivPhi    = phi;
        ivUpdate = upd;
        break;
    }
    if (!ivPhi) return false;

    // ── 识别 cond：cmp(ivUpdate, 不变 bound)，且只被 br 使用 ────────────
    auto *cmpInst = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmpInst || cmpInst->parent_ != header) return false;
    if (cmpInst->use_list_.size() != 1) return false;
    auto op = cmpInst->icmp_op_;
    if (op != ICmpInst::ICMP_SLT && op != ICmpInst::ICMP_SLE &&
        op != ICmpInst::ICMP_SGT && op != ICmpInst::ICMP_SGE)
        return false;
    Value *bound   = nullptr;
    bool   ivIsLeft = true;
    if (cmpInst->get_operand(0) == ivUpdate) {
        bound = cmpInst->get_operand(1); ivIsLeft = true;
    } else if (cmpInst->get_operand(1) == ivUpdate) {
        bound = cmpInst->get_operand(0); ivIsLeft = false;
    } else {
        return false;
    }
    if (auto *bi = dynamic_cast<Instruction *>(bound))
        if (bi->parent_ == header) return false;
    if (dynamicStride &&
        (!ivIsLeft || op != ICmpInst::ICMP_SLT))
        return false;

    // ── body 可克隆性 ───────────────────────────────────────────────────
    UnrollCost unrollCost;
    for (auto inst : header->instr_list_) {
        if (inst->is_phi() || inst == cmpInst || inst->isTerminator()) continue;
        if (inst->is_call() || inst->is_alloca()) return false;
        bool canClone = dynamic_cast<BinaryInst *>(inst) ||
                        dynamic_cast<UnaryInst *>(inst) ||
                        dynamic_cast<ICmpInst *>(inst) ||
                        dynamic_cast<FCmpInst *>(inst) ||
                        dynamic_cast<SelectInst *>(inst) ||
                        dynamic_cast<GetElementPtrInst *>(inst) ||
                        dynamic_cast<LoadInst *>(inst) ||
                        dynamic_cast<StoreInst *>(inst) ||
                        dynamic_cast<ZextInst *>(inst) ||
                        dynamic_cast<FpToSiInst *>(inst) ||
                        dynamic_cast<SiToFpInst *>(inst) ||
                        dynamic_cast<Bitcast *>(inst);
        if (!canClone) return false;
        ++unrollCost.bodyInstructions;
        if (inst->is_load() || inst->is_store() || inst->is_gep())
            ++unrollCost.memoryOperations;
        if (inst->type_->tid_ == Type::VectorTyID)
            unrollCost.hasVectorOperations = true;
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            if (inst->get_operand(i)->type_->tid_ ==
                Type::VectorTyID) {
                unrollCost.hasVectorOperations = true;
                break;
            }
        }
    }

    // ── 循环外使用清点：只允许 (a) exitBB 中的 phi（入边=header），
    //    (b) 其他位置的使用（exitBB 唯一出口支配它们，事后补 phi）─────
    struct OutsideUse { Instruction *user; unsigned idx; };
    std::map<Value *, std::vector<OutsideUse>> liveOuts; // 需补 phi 的值
    for (auto inst : header->instr_list_) {
        if (inst == cmpInst || inst->isTerminator()) continue;
        for (const auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_ || user->parent_ == header) continue;
            if (user->parent_ == exitBB && user->is_phi()) continue; // (a)
            liveOuts[inst].push_back({user, use.arg_no_});           // (b)
        }
    }

    countLoopStates(headerPhis, ivPhi, unrollCost);
    int N = chooseUnrollFactor(unrollCost);
    if (dynamicStride && unrollCost.memoryOperations == 0 &&
        unrollCost.bodyInstructions <= 18 &&
        unrollCost.integerStates + unrollCost.pointerStates <= 2)
        N = 8;
    else if (dynamicStride && unrollCost.memoryOperations == 0 &&
        unrollCost.bodyInstructions <= 24 &&
        unrollCost.integerStates + unrollCost.pointerStates <= 2)
        N = 4;
    if (N == 0)
        return false;
    if (dynamicStride && N != 2 && N != 4 && N != 8)
        return false;

    int adj = 0;
    if (!dynamicStride) {
        if (!computeBoundAdjustment(N - 1, strideVal, adj))
            return false;
        if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
            if (strideVal > 0 && cb->value_ < adj) return false;
            if (strideVal < 0 &&
                cb->value_ > adj + std::numeric_limits<int>::max())
                return false;
        }
    }

    if (std::getenv("DEBUG_LOOP_UNROLL"))
        std::cerr << "[LoopUnroll] func=" << func->name_
                  << " header=" << header->name_
                  << " factor=" << N
                  << " form=dowhile"
                  << (dynamicStride ? " dynamic-stride" : "")
                  << " body=" << unrollCost.bodyInstructions
                  << " gpr-state="
                  << unrollCost.integerStates + unrollCost.pointerStates
                  << " fpr-state="
                  << unrollCost.floatingStates + unrollCost.vectorStates
                  << " memory=" << unrollCost.memoryOperations << "\n";

    // ── 变换 ────────────────────────────────────────────────────────────
    // 1. boundMain = bound - adj.  For a dynamic step, this is only
    // consumed on the guarded path below.
    Value *boundMain;
    Value *dynamicAdjustment = strideValue;
    if (dynamicStride) {
        if (N != 2) {
            auto *factor = new ConstantInt(module->int32_ty_, N - 1);
            auto *scaled = new BinaryInst(module->int32_ty_,
                                          Instruction::Mul, strideValue,
                                          factor, preheader,
                                          /*no-insert*/true);
            preheader->add_instruction_before_terminator(scaled);
            dynamicAdjustment = scaled;
        }
        auto *sub = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                   bound, dynamicAdjustment, preheader,
                                   /*no-insert*/true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    } else if (auto *cb = dynamic_cast<ConstantInt *>(bound)) {
        boundMain = new ConstantInt(module->int32_ty_, cb->value_ - adj);
    } else {
        auto *adjC = new ConstantInt(module->int32_ty_, adj);
        auto *sub  = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                    bound, adjC, preheader, /*no-insert*/true);
        preheader->add_instruction_before_terminator(sub);
        boundMain = sub;
    }

    // 2. checkBlock：iv_init 满足 N 次余量则进主循环，否则走原循环
    auto *checkBlock = new BasicBlock(module, "unroll_guard", func);
    ICmpInst *cmpEnter =
        ivIsLeft ? new ICmpInst(op, getInitVal(ivPhi), boundMain, checkBlock)
                 : new ICmpInst(op, boundMain, getInitVal(ivPhi), checkBlock);
    Value *enterCondition = cmpEnter;
    if (!dynamicStride && !dynamic_cast<ConstantInt *>(bound))
        enterCondition = guardCondition(
            cmpEnter,
            buildBoundAdjustmentGuard(bound, adj, module, checkBlock),
            module, checkBlock);
    if (dynamicStride) {
        auto *zero = new ConstantInt(module->int32_ty_, 0);
        auto *intMin = new ConstantInt(module->int32_ty_,
                                       std::numeric_limits<int>::min());
        auto *intMax = new ConstantInt(module->int32_ty_,
                                       std::numeric_limits<int>::max());
        auto *one = new ConstantInt(module->int32_ty_, 1);

        auto *stepPositive = new ICmpInst(ICmpInst::ICMP_SGT, strideValue,
                                          zero, checkBlock);
        auto *stepScaleSafe = new ICmpInst(
            ICmpInst::ICMP_SLE, strideValue,
            new ConstantInt(module->int32_ty_,
                            std::numeric_limits<int>::max() / (N - 1)),
            checkBlock);
        auto *startNonNegative = new ICmpInst(
            ICmpInst::ICMP_SGE, getInitVal(ivPhi), zero, checkBlock);
        auto *lowerLimit = new BinaryInst(module->int32_ty_,
                                          Instruction::Add, intMin,
                                          dynamicAdjustment, checkBlock);
        auto *boundLowerSafe = new ICmpInst(ICmpInst::ICMP_SGE, bound,
                                            lowerLimit, checkBlock);
        auto *maxMinusStep = new BinaryInst(module->int32_ty_,
                                             Instruction::Sub, intMax,
                                             strideValue, checkBlock);
        auto *upperLimit = new BinaryInst(module->int32_ty_,
                                          Instruction::Add, maxMinusStep,
                                          one, checkBlock);
        auto *boundUpperSafe = new ICmpInst(ICmpInst::ICMP_SLE, bound,
                                            upperLimit, checkBlock);
        auto *positiveAndLower = new BinaryInst(
            module->int1_ty_, Instruction::And, stepPositive,
            boundLowerSafe, checkBlock);
        auto *withScale = new BinaryInst(module->int1_ty_,
                                         Instruction::And,
                                         positiveAndLower,
                                         stepScaleSafe, checkBlock);
        auto *withUpper = new BinaryInst(module->int1_ty_,
                                         Instruction::And,
                                         withScale,
                                         boundUpperSafe, checkBlock);
        auto *safe = new BinaryInst(module->int1_ty_, Instruction::And,
                                    withUpper, startNonNegative, checkBlock);
        enterCondition = new BinaryInst(module->int1_ty_, Instruction::And,
                                        safe, cmpEnter, checkBlock);
    }

    // 3. mainLoop：phi + N 份克隆
    auto *mainLoop = new BasicBlock(module, "unroll_main", func);
    std::unordered_map<PhiInst *, PhiInst *> phiToMain;
    for (int i = (int)headerPhis.size() - 1; i >= 0; i--) {
        auto *phi     = headerPhis[i];
        auto *mainPhi = PhiInst::create_phi(phi->type_, mainLoop);
        mainLoop->add_instruction_front(mainPhi);
        mainPhi->addIncoming(getInitVal(phi), checkBlock);
        if (dynamicStride && phi == ivPhi)
            mainPhi->setSemFlag(SemFlag::KnownNonNegative);
        phiToMain[phi] = mainPhi;
    }

    std::unordered_map<Value *, Value *> iterMap;
    for (auto phi : headerPhis)
        iterMap[phi] = phiToMain[phi];
    std::unordered_map<Value *, Value *> finalMap; // 最后一轮的完整映射
    std::unordered_map<PhiInst *, Value *> curPhiVals;
    for (auto *phi : headerPhis)
        curPhiVals[phi] = phiToMain[phi];

    // For a guarded register-only loop, compute the unrolled iterations'
    // state-independent expressions before updating loop-carried scalar
    // states.  This keeps only the final contributions live, while exposing
    // the two long arithmetic chains to instruction scheduling.
    std::set<Instruction *> deferredStateUpdates;
    if (dynamicStride && (N == 2 || N == 4 || N == 8) &&
        unrollCost.memoryOperations == 0) {
        for (auto *inst : header->instr_list_) {
            if (inst->is_phi() || inst == cmpInst || inst->isTerminator())
                continue;
            bool dependsOnState = false;
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                Value *operand = inst->get_operand(i);
                auto *phi = dynamic_cast<PhiInst *>(operand);
                auto *operandInst = dynamic_cast<Instruction *>(operand);
                if ((phi && phi != ivPhi &&
                     phiToMain.count(phi)) ||
                    (operandInst &&
                     deferredStateUpdates.count(operandInst))) {
                    dependsOnState = true;
                    break;
                }
            }
            if (dependsOnState)
                deferredStateUpdates.insert(inst);
        }
    }

    std::vector<UnrolledModuloRecurrence> modularRecurrences;
    std::unordered_map<PhiInst *, std::size_t> modularRecurrenceIndex;
    std::set<Instruction *> modularUpdateInstructions;

    // For an unrolled recurrence
    //   state.next = (state + contribution) % M
    // combine the first N-1 contributions behind one remainder.  The final
    // iteration stays in its original form because rotated loops may export
    // an intermediate value from that iteration to the dedicated exit block.
    // This preserves that live-out mapping while shortening the serial
    // remainder chain from N steps to two.
    if (!deferredStateUpdates.empty() && N > 2) {
        for (auto *phi : headerPhis) {
            if (phi == ivPhi || phi->type_->tid_ != Type::IntegerTyID)
                continue;
            auto *remainder = dynamic_cast<BinaryInst *>(getBackVal(phi));
            if (!remainder || remainder->parent_ != header ||
                remainder->op_id_ != Instruction::SRem)
                continue;
            std::set<BasicBlock *> updateBlocks{header};
            UnrolledModuloRecurrence candidate;
            if (!prepareModuloRecurrence(
                    phi, remainder, getInitVal(phi), updateBlocks,
                    headerPhis, ivPhi, N, true, candidate))
                continue;

            modularRecurrenceIndex[phi] = modularRecurrences.size();
            modularUpdateInstructions.insert(
                candidate.updateChain.begin(), candidate.updateChain.end());
            modularRecurrences.push_back(std::move(candidate));
            if (std::getenv("DEBUG_LOOP_UNROLL"))
                std::cerr << "[LoopUnroll] func=" << func->name_
                          << " header=" << header->name_
                          << " modular-prefix=" << N - 1 << "\n";
        }
    }

    auto cloneBodyInstruction =
        [&](Instruction *inst,
            std::unordered_map<Value *, Value *> &localMap) -> bool {
            auto *newInst = cloneInst(inst, mainLoop, localMap);
            if (!newInst)
                return false;
            if (dynamicStride && inst == ivUpdate) {
                newInst->setSemFlag(SemFlag::NoSignedWrap);
                newInst->setSemFlag(SemFlag::KnownNonNegative);
            }
            localMap[inst] = newInst;
            return true;
        };

    if (deferredStateUpdates.empty()) {
        for (int iter = 0; iter < N; iter++) {
            std::unordered_map<Value *, Value *> localMap = iterMap;
            for (auto inst : header->instr_list_) {
                if (inst->is_phi() || inst == cmpInst ||
                    inst->isTerminator())
                    continue;
                if (!cloneBodyInstruction(inst, localMap))
                    return false;
            }
            for (auto phi : headerPhis) {
                Value *bv = getBackVal(phi);
                auto it = localMap.find(bv);
                curPhiVals[phi] =
                    (it != localMap.end()) ? it->second : bv;
            }
            for (auto phi : headerPhis)
                iterMap[phi] = curPhiVals[phi];
            if (iter == N - 1)
                finalMap = std::move(localMap);
        }
    } else {
        std::vector<std::unordered_map<Value *, Value *>> iterationMaps;
        iterationMaps.reserve(N);

        // Materialize the next IVs first, then clone independent instructions
        // in lockstep across iterations.  The resulting order interleaves
        // equal-depth operations from the two dependency chains.
        Value *ivBack = getBackVal(ivPhi);
        for (int iter = 0; iter < N; ++iter) {
            std::unordered_map<Value *, Value *> localMap = iterMap;
            if (!cloneBodyInstruction(ivUpdate, localMap))
                return false;
            auto ivIt = localMap.find(ivBack);
            if (ivIt == localMap.end())
                return false;
            curPhiVals[ivPhi] = ivIt->second;
            iterMap[ivPhi] = ivIt->second;
            iterationMaps.push_back(std::move(localMap));
        }
        for (auto it = header->instr_list_.begin();
             it != header->instr_list_.end(); ++it) {
            Instruction *inst = *it;
            if (inst->is_phi() || inst == cmpInst ||
                inst == ivUpdate || inst->isTerminator() ||
                deferredStateUpdates.count(inst))
                continue;

            Instruction *flagUser = nullptr;
            if (dynamic_cast<ICmpInst *>(inst)) {
                auto next = std::next(it);
                if (next != header->instr_list_.end()) {
                    auto *select = dynamic_cast<SelectInst *>(*next);
                    if (select && select->get_operand(0) == inst &&
                        !deferredStateUpdates.count(select))
                        flagUser = select;
                }
            }

            for (auto &localMap : iterationMaps) {
                if (!cloneBodyInstruction(inst, localMap))
                    return false;
                if (flagUser &&
                    !cloneBodyInstruction(flagUser, localMap))
                    return false;
            }
            if (flagUser)
                ++it;
        }

        std::unordered_map<PhiInst *, Value *> modularPrefixStates;
        for (const auto &recurrence : modularRecurrences) {
            Value *accumulator = phiToMain[recurrence.state];
            for (int iter = 0; iter < N - 1; ++iter) {
                auto &localMap = iterationMaps[iter];
                for (const auto &term : recurrence.contributionTerms) {
                    auto mapped = localMap.find(term.value);
                    Value *contribution =
                        mapped != localMap.end() ? mapped->second : term.value;
                    accumulator = new BinaryInst(
                        module->int32_ty_,
                        term.sign > 0 ? Instruction::Add : Instruction::Sub,
                        accumulator,
                        contribution, mainLoop);
                }
            }
            Value *combined = buildBoundedModulo(
                accumulator, recurrence.prefixLower,
                recurrence.prefixUpper, recurrence.modulus->value_,
                module, mainLoop);
            modularPrefixStates[recurrence.state] = combined;
        }

        // Then emit each recurrence update in iteration order so its exact
        // scalar semantics remain unchanged.  Modular recurrences defer their
        // private update chain until the final iteration; that iteration uses
        // the safely combined prefix state above.
        for (int iter = 0; iter < N; ++iter) {
            auto &localMap = iterationMaps[iter];
            if (iter == N - 1)
                for (const auto &[phi, value] : modularPrefixStates)
                    curPhiVals[phi] = value;
            for (auto *phi : headerPhis)
                if (phi != ivPhi)
                    localMap[phi] = curPhiVals[phi];
            for (auto *inst : header->instr_list_) {
                if (!deferredStateUpdates.count(inst))
                    continue;
                if (iter < N - 1 &&
                    modularUpdateInstructions.count(inst))
                    continue;
                if (!cloneBodyInstruction(inst, localMap))
                    return false;
            }
            for (auto *phi : headerPhis) {
                if (phi == ivPhi)
                    continue;
                if (iter < N - 1 && modularRecurrenceIndex.count(phi))
                    continue;
                Value *back = getBackVal(phi);
                auto stateIt = localMap.find(back);
                if (stateIt == localMap.end())
                    return false;
                auto recurrenceIt = modularRecurrenceIndex.find(phi);
                if (iter == N - 1 &&
                    recurrenceIt != modularRecurrenceIndex.end()) {
                    const auto &recurrence =
                        modularRecurrences[recurrenceIt->second];
                    auto *finalRemainder =
                        dynamic_cast<BinaryInst *>(stateIt->second);
                    if (!finalRemainder ||
                        finalRemainder->op_id_ != Instruction::SRem)
                        return false;
                    Value *lowered = buildBoundedModulo(
                        finalRemainder->get_operand(0),
                        recurrence.finalLower, recurrence.finalUpper,
                        recurrence.modulus->value_, module, mainLoop);
                    localMap[back] = lowered;
                    stateIt = localMap.find(back);
                }
                curPhiVals[phi] = stateIt->second;
            }
            if (iter == N - 1)
                finalMap = localMap;
        }
    }

    auto mapFinal = [&](Value *v) -> Value * {
        if (auto *p = dynamic_cast<PhiInst *>(v))
            if (curPhiVals.count(p)) return curPhiVals[p];
        auto it = finalMap.find(v);
        return it != finalMap.end() ? it->second : v;
    };

    Value *ivOut = curPhiVals[ivPhi]; // 主循环出口处的 iv（= 映射后的 ivUpdate）
    ICmpInst *cmpMain =
        ivIsLeft ? new ICmpInst(op, ivOut, boundMain, mainLoop)
                 : new ICmpInst(op, boundMain, ivOut, mainLoop);
    for (auto phi : headerPhis)
        phiToMain[phi]->addIncoming(curPhiVals[phi], mainLoop);

    // 4. remCheck：还有余数则进原循环，否则直接 exit
    auto *remCheck = new BasicBlock(module, "unroll_rem_guard", func);
    ICmpInst *cmpRem =
        ivIsLeft ? new ICmpInst(op, ivOut, bound, remCheck)
                 : new ICmpInst(op, bound, ivOut, remCheck);
    new BranchInst(cmpRem, header, exitBB, remCheck);

    // 5. 分支接线（BranchInst 构造器维护 CFG 链接）
    new BranchInst(enterCondition, mainLoop, header, checkBlock);
    new BranchInst(cmpMain, mainLoop, remCheck, mainLoop);

    // 6. preheader → checkBlock（替换原指向 header 的边）
    auto *preBr = preheader->get_terminator();
    for (unsigned i = 0; i < preBr->num_ops_; i++) {
        if (preBr->get_operand(i) == header)
            preBr->set_operand(i, checkBlock);
    }
    preheader->remove_succ_basic_block(header);
    preheader->add_succ_basic_block(checkBlock);
    header->remove_pre_basic_block(preheader);
    checkBlock->add_pre_basic_block(preheader);

    // 7. 原循环 phi：preheader 入边 → checkBlock；新增 remCheck 入边
    for (auto phi : headerPhis) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == preheader) {
                phi->set_operand(i + 1, checkBlock);
                break;
            }
        }
        phi->addIncoming(curPhiVals[phi], remCheck);
    }

    // 8. exitBB phi：补 remCheck 入边（值 = 主循环末值映射）
    for (auto inst : exitBB->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *fromHeader = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2)
            if (phi->get_operand(i + 1) == header)
                fromHeader = phi->get_operand(i);
        if (!fromHeader) return false; // 不应发生：唯一前驱是 header
        phi->addIncoming(mapFinal(fromHeader), remCheck);
    }

    // 9. 其他循环外使用：在 exitBB 顶部补汇合 phi 后改写
    // Preserve the source instruction order; pointer-key order changes under
    // ASLR and `add_instruction_front` would otherwise expose that variation.
    for (auto *val : header->instr_list_) {
        auto liveOutIt = liveOuts.find(val);
        if (liveOutIt == liveOuts.end()) continue;
        auto &uses = liveOutIt->second;
        auto *mergePhi = PhiInst::create_phi(val->type_, exitBB);
        exitBB->add_instruction_front(mergePhi);
        mergePhi->addIncoming(val, header);
        mergePhi->addIncoming(mapFinal(val), remCheck);
        for (auto &u : uses)
            u.user->set_operand(u.idx, mergePhi);
    }

    return true;
}

// ── Entry points ──────────────────────────────────────────────────────────

bool LoopUnroll::runOnFunction(Function *func, BasicAliasAnalysis &BAA) {
    if (func->basic_blocks_.empty()) return false;

    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty()) return false;

    // Innermost first。unroll 成功会改 CFG（preheader 改指 headerMain），
    // 快照内其余 Loop 结构随之过期——与迁移前行为一致：外层循环因
    // blocks.size()!=2 本就不会命中，错过的机会留给下一次调度。
    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops) {
        if (tryUnroll(*loop, func, func->parent_, BAA)) {
            changed = true;
            continue;
        }
        if (tryUnrollStructured(*loop, func, func->parent_)) {
            changed = true;
            continue;
        }
        if (tryUnrollStatefulWhileCFGRegion(*loop, func, func->parent_)) {
            changed = true;
            continue;
        }
        if (tryUnrollCFGRegion(*loop, func, func->parent_)) {
            changed = true;
            continue;
        }
        if (tryUnrollDoWhile(*loop, func, func->parent_))
            changed = true;
    }

    if (changed)
        func->set_instr_name();
    return changed;
}

void LoopUnroll::execute(Module *module) {
    BasicAliasAnalysis BAA;
    BAA.analyze(module);
    bool changed = false;
    for (auto func : module->function_list_)
        if (!func->is_declaration())
            changed |= runOnFunction(func, BAA);
    if (changed) {
        LCSSA lcssa;
        lcssa.execute(module);
    }
}
