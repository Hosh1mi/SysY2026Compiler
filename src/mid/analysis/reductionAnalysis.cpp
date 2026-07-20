#include "../../include/mid/analysis/reductionAnalysis.hpp"
#include "../../include/mid/analysis/loopAccessAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

namespace {

bool availableOutsideLoop(Value *value, Loop *loop) {
    if (dynamic_cast<Constant *>(value)) return true;
    if (dynamic_cast<GlobalVariable *>(value)) return true;
    if (dynamic_cast<Argument *>(value)) return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst) return true;
    return !loop->blocks.count(inst->parent_);
}

} // namespace

bool ReductionAnalysis::detectScalarExpandableNest(
    Loop *inner, ScalarReductionNestInfo &out) {
    if (!inner) return false;

    Loop *parent = inner->parent;
    if (!parent) return false;
    if (!inner->hasCanonicalIV() || !parent->hasCanonicalIV()) return false;

    PhiInst *innerIV = inner->canonicalIV;
    PhiInst *parentIV = parent->canonicalIV;
    Value *innerBound = inner->tripCount;
    Value *parentBound = parent->tripCount;

    if (!inner->preheader || !inner->singleLatch() || !inner->singleExit())
        return false;

    LoopAccessAnalysis loopAccess(*AA_);

    std::vector<ScalarReductionInfo> reductions;
    for (auto *inst : inner->header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == innerIV) continue;
        if (phi->type_->tid_ != Type::IntegerTyID) return false;
        if (phi->num_ops_ != 4) return false;

        ScalarReductionInfo reduction{};
        reduction.sum_phi = phi;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (src == inner->preheader) {
                reduction.sum_init = phi->get_operand(i);
            } else if (src == inner->singleLatch()) {
                reduction.sum_latch = phi->get_operand(i);
            }
        }
        if (!reduction.sum_init || !reduction.sum_latch) return false;
        if (!availableOutsideLoop(reduction.sum_init, parent)) return false;
        reductions.push_back(reduction);
    }
    if (reductions.empty()) return false;

    LoopAccessInfo bodyAccess = loopAccess.collect(inner);
    if (bodyAccess.has_store || bodyAccess.has_call) return false;
    for (auto *gep : bodyAccess.all_geps) {
        if (!loopAccess.isAffineOverAncestorIVs(gep, inner)) return false;
        if (!LoopAccessAnalysis::isGlobalOrArgument(gep->get_operand(0)))
            return false;
    }

    BasicBlock *innerExit = inner->singleExit();
    std::vector<StoreInst *> exitStores;
    for (auto *inst : innerExit->instr_list_) {
        if (inst->is_store()) exitStores.push_back(static_cast<StoreInst *>(inst));
        if (inst->is_call()) return false;
    }
    if (exitStores.size() != reductions.size()) return false;

    std::vector<bool> matched(reductions.size(), false);
    for (auto *store : exitStores) {
        Value *stored = store->get_operand(0);
        int matchedIndex = -1;
        for (size_t i = 0; i < reductions.size(); i++) {
            if (!matched[i] && reductions[i].sum_phi == stored) {
                matchedIndex = static_cast<int>(i);
                break;
            }
        }
        if (matchedIndex < 0) return false;
        matched[matchedIndex] = true;

        auto &reduction = reductions[matchedIndex];
        reduction.store_inst = store;

        auto *storeGEP = dynamic_cast<GetElementPtrInst *>(store->get_operand(1));
        if (!storeGEP || storeGEP->num_ops_ < 2) return false;

        AffineExpr firstIndex = AA_->analyze(storeGEP->get_operand(1));
        if (!firstIndex.isZero()) return false;

        unsigned last = storeGEP->num_ops_ - 1;
        AffineExpr lastIndex = AA_->analyze(storeGEP->get_operand(last));
        if (!lastIndex.valid || lastIndex.constant != 0 ||
            lastIndex.coeffs.size() != 1 || lastIndex.coeffOf(parentIV) != 1)
            return false;

        for (unsigned m = 2; m < last; m++) {
            AffineExpr midIndex = AA_->analyze(storeGEP->get_operand(m));
            if (!midIndex.valid) return false;
            if (midIndex.coeffOf(parentIV) != 0) return false;
            if (midIndex.coeffOf(innerIV) != 0) return false;
        }

        reduction.gep_store = storeGEP;
        reduction.base_store = storeGEP->get_operand(0);
        if (!LoopAccessAnalysis::isGlobalOrArgument(reduction.base_store))
            return false;
        reduction.inner_dim =
            LoopAccessAnalysis::innermostArrayDim(reduction.base_store);
        if (reduction.inner_dim <= 0) return false;
    }

    for (auto *bb : parent->blocks) {
        if (inner->blocks.count(bb)) continue;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) {
                auto *store = static_cast<StoreInst *>(inst);
                bool isReductionStore = false;
                for (auto &reduction : reductions) {
                    if (reduction.store_inst == store) {
                        isReductionStore = true;
                        break;
                    }
                }
                if (!isReductionStore) return false;
            }
            if (inst->is_call()) return false;
        }
    }

    out.inner_loop = inner;
    out.parent_loop = parent;
    out.body_geps = std::move(bodyAccess.all_geps);
    out.reductions = std::move(reductions);
    out.inner_bound = innerBound;
    out.parent_bound = parentBound;
    return true;
}

bool ReductionAnalysis::isScalarExpansionMemoryLegal(
    const ScalarReductionNestInfo &info) const {
    if (!info.parent_loop || !info.parent_loop->canonicalIV) return false;
    PhiInst *parentIV = info.parent_loop->canonicalIV;

    auto exactParentCoordinate = [&](Value *index) {
        AffineExpr expr = AA_->analyze(index);
        return expr.valid && expr.constant == 0 && expr.coeffs.size() == 1 &&
               expr.coeffOf(parentIV) == 1;
    };

    for (const auto &reduction : info.reductions) {
        if (!reduction.gep_store || !reduction.base_store) return false;
        unsigned storeLast = reduction.gep_store->num_ops_ - 1;
        if (!exactParentCoordinate(
                reduction.gep_store->get_operand(storeLast)))
            return false;

        for (auto *bodyGEP : info.body_geps) {
            Value *bodyBase = bodyGEP->get_operand(0);
            if (bodyBase != reduction.base_store) {
                // Distinct globals are disjoint.  Distinct pointer arguments
                // may alias, so without an argument-alias oracle they cannot
                // justify delaying the output store.
                if (dynamic_cast<GlobalVariable *>(bodyBase) &&
                    dynamic_cast<GlobalVariable *>(reduction.base_store))
                    continue;
                return false;
            }

            // Scalar expansion delays every reduction store until after the
            // interchanged compute nest.  This is legal when any load from the
            // same object has the identical parent-loop coordinate: different
            // parent iterations then address disjoint slices, while loads and
            // the delayed store within one iteration retain read-before-write.
            bool sameParentSlice = false;
            for (unsigned i = 1; i < bodyGEP->num_ops_; ++i) {
                if (exactParentCoordinate(bodyGEP->get_operand(i))) {
                    sameParentSlice = true;
                    break;
                }
            }
            if (!sameParentSlice) return false;
        }
    }
    return true;
}
