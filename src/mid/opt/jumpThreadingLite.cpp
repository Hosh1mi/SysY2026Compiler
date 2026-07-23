#include "../../include/mid/opt/jumpThreadingLite.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"

#include <optional>
#include <set>
#include <vector>

namespace {

struct PhiRepairEdit {
    PhiInst *phi = nullptr;
    int incomingIdx = -1;       // block operand index for the old mid incoming
    Value *replacement = nullptr;
    bool removeMidIncoming = false;
};

bool hasTerminatorEdge(BasicBlock *from, BasicBlock *to) {
    auto *term = from ? from->get_terminator() : nullptr;
    if (!term) return false;
    for (unsigned i = 0; i < term->num_ops_; ++i) {
        if (term->get_operand(i) == to)
            return true;
    }
    return false;
}

int countTerminatorEdges(BasicBlock *from, BasicBlock *to) {
    auto *term = from ? from->get_terminator() : nullptr;
    if (!term) return 0;
    int count = 0;
    for (unsigned i = 0; i < term->num_ops_; ++i) {
        if (term->get_operand(i) == to)
            ++count;
    }
    return count;
}

bool sameValue(Value *lhs, Value *rhs) {
    if (lhs == rhs) return true;
    int lhsConst = 0;
    int rhsConst = 0;
    return getIntegerConstantValue(lhs, lhsConst) &&
           getIntegerConstantValue(rhs, rhsConst) &&
           lhsConst == rhsConst;
}

bool isLoopHeader(BasicBlock *bb, const std::set<BasicBlock *> &loopHeaders) {
    return loopHeaders.find(bb) != loopHeaders.end();
}

bool isUnsafeLoopThread(BasicBlock *pred, BasicBlock *mid, BasicBlock *chosenSucc,
                        const LoopInfo &LI,
                        const std::set<BasicBlock *> &loopHeaders) {
    if (isLoopHeader(mid, loopHeaders) || isLoopHeader(chosenSucc, loopHeaders))
        return true;

    // Mirroring LLVM's conservative loop-header stance: do not create a new
    // edge to a dominating block, which would be a new backedge candidate.
    if (LI.dominates(chosenSucc, pred))
        return true;

    // If the existing edge is already a backedge, keep loop structure intact.
    if (LI.dominates(mid, pred))
        return true;

    return false;
}

void removeIncomingFromPred(BasicBlock *succ, BasicBlock *pred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = static_cast<int>(phi->num_ops_) - 1; i >= 1; i -= 2) {
            if (phi->get_operand(i) == pred)
                phi->remove_operands(i - 1, i);
        }
    }
}

void collectEdgeFacts(BasicBlock *pred, BasicBlock *succ, BoolFactMap &boolFacts,
                      ICmpFactMap &cmpFacts) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br || br->num_ops_ != 3) return;
    auto *trueDest = static_cast<BasicBlock *>(br->get_operand(1));
    auto *falseDest = static_cast<BasicBlock *>(br->get_operand(2));
    if (trueDest != succ && falseDest != succ) return;
    recordAssumedBool(br->get_operand(0), trueDest == succ, boolFacts, cmpFacts);
}

bool isAllowedPhiIncomingValue(Value *value, BasicBlock *pred,
                               const LoopInfo &LI) {
    if (dynamic_cast<Constant *>(value)) return true;
    if (dynamic_cast<Argument *>(value)) return true;
    if (dynamic_cast<GlobalVariable *>(value)) return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst || !inst->parent_) return false;
    if (inst->parent_->parent_ != pred->parent_) return false;
    return LI.dominates(inst->parent_, pred);
}

Value *substitutePhiOnEdge(Value *value, BasicBlock *mid, BasicBlock *pred,
                           std::set<PhiInst *> &visiting) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    if (!phi || phi->parent_ != mid) return value;
    if (!visiting.insert(phi).second) return nullptr;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) != pred) continue;
        Value *incoming = phi->get_operand(i);
        if (auto *incomingPhi = dynamic_cast<PhiInst *>(incoming)) {
            if (incomingPhi->parent_ == mid)
                return substitutePhiOnEdge(incomingPhi, mid, pred, visiting);
        }
        return incoming;
    }
    return nullptr;
}

Value *substitutePhiOnEdge(Value *value, BasicBlock *mid, BasicBlock *pred) {
    std::set<PhiInst *> visiting;
    return substitutePhiOnEdge(value, mid, pred, visiting);
}

std::optional<bool> evaluateBoolWithSubstitution(Value *value, BasicBlock *mid,
                                                 BasicBlock *pred,
                                                 const BoolFactMap &boolFacts,
                                                 const ICmpFactMap &cmpFacts) {
    Value *substituted = substitutePhiOnEdge(value, mid, pred);
    if (!substituted) return std::nullopt;

    auto known = getKnownBool(substituted, boolFacts, cmpFacts);
    if (known.has_value()) return known;

    auto *icmp = dynamic_cast<ICmpInst *>(substituted);
    if (!icmp || icmp->parent_ != mid) return std::nullopt;

    Value *lhs = substitutePhiOnEdge(icmp->get_operand(0), mid, pred);
    Value *rhs = substitutePhiOnEdge(icmp->get_operand(1), mid, pred);
    if (!lhs || !rhs) return std::nullopt;

    int lhsConst = 0;
    int rhsConst = 0;
    if (!getIntegerConstantValue(lhs, lhsConst) ||
        !getIntegerConstantValue(rhs, rhsConst))
        return std::nullopt;

    switch (icmp->icmp_op_) {
        case ICmpInst::ICMP_EQ:  return lhsConst == rhsConst;
        case ICmpInst::ICMP_NE:  return lhsConst != rhsConst;
        case ICmpInst::ICMP_SGT: return lhsConst > rhsConst;
        case ICmpInst::ICMP_SGE: return lhsConst >= rhsConst;
        case ICmpInst::ICMP_SLT: return lhsConst < rhsConst;
        case ICmpInst::ICMP_SLE: return lhsConst <= rhsConst;
        case ICmpInst::ICMP_UGT: return static_cast<unsigned>(lhsConst) >  static_cast<unsigned>(rhsConst);
        case ICmpInst::ICMP_UGE: return static_cast<unsigned>(lhsConst) >= static_cast<unsigned>(rhsConst);
        case ICmpInst::ICMP_ULT: return static_cast<unsigned>(lhsConst) <  static_cast<unsigned>(rhsConst);
        case ICmpInst::ICMP_ULE: return static_cast<unsigned>(lhsConst) <= static_cast<unsigned>(rhsConst);
    }
    return std::nullopt;
}

bool isThreadableMidBlock(BasicBlock *bb, ICmpInst *&cmpOut) {
    cmpOut = nullptr;
    auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!term || term->num_ops_ != 3) return false;

    for (auto *inst : bb->instr_list_) {
        if (inst->is_phi()) continue;
        if (inst == term) break;
        if (auto *icmp = dynamic_cast<ICmpInst *>(inst)) {
            if (cmpOut != nullptr) return false;
            cmpOut = icmp;
            continue;
        }
        return false;
    }
    return true;
}

bool redirectPredEdge(BasicBlock *pred, BasicBlock *oldTarget,
                      BasicBlock *newTarget) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br) return false;

    if (br->num_ops_ == 1) {
        if (br->get_operand(0) != oldTarget) return false;
        pred->remove_succ_basic_block(oldTarget);
        oldTarget->remove_pre_basic_block(pred);
        br->set_operand(0, newTarget);
        pred->add_succ_basic_block(newTarget);
        newTarget->add_pre_basic_block(pred);
        return true;
    }

    if (br->num_ops_ != 3) return false;
    for (int i = 1; i <= 2; ++i) {
        if (br->get_operand(i) != oldTarget) continue;
        pred->remove_succ_basic_block(oldTarget);
        oldTarget->remove_pre_basic_block(pred);
        br->set_operand(i, newTarget);
        pred->add_succ_basic_block(newTarget);
        newTarget->add_pre_basic_block(pred);
        return true;
    }
    return false;
}

bool planSuccessorPhiRepairs(BasicBlock *chosenSucc, BasicBlock *mid,
                             BasicBlock *pred, const LoopInfo &LI,
                             std::vector<PhiRepairEdit> &edits) {
    for (auto *inst : chosenSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);

        int incomingIdx = -1;
        int existingPredIdx = -1;
        for (int i = 1; i < static_cast<int>(phi->num_ops_); i += 2) {
            if (phi->get_operand(i) == mid) {
                if (incomingIdx >= 0)
                    return false;
                incomingIdx = i;
                continue;
            }
            if (phi->get_operand(i) == pred) {
                if (existingPredIdx >= 0)
                    return false;
                existingPredIdx = i;
            }
        }
        if (incomingIdx < 0) continue;

        Value *replacement = substitutePhiOnEdge(phi->get_operand(incomingIdx - 1),
                                                 mid, pred);
        if (!replacement || !isAllowedPhiIncomingValue(replacement, pred, LI))
            return false;

        if (existingPredIdx >= 0) {
            if (!sameValue(phi->get_operand(existingPredIdx - 1), replacement))
                return false;
            edits.push_back({phi, incomingIdx, replacement,
                             /*removeMidIncoming=*/true});
            continue;
        }

        edits.push_back({phi, incomingIdx, replacement,
                         /*removeMidIncoming=*/false});
    }
    return true;
}

void applySuccessorPhiRepairs(const std::vector<PhiRepairEdit> &edits,
                              BasicBlock *pred) {
    for (const auto &edit : edits) {
        if (edit.removeMidIncoming) {
            edit.phi->remove_operands(edit.incomingIdx - 1, edit.incomingIdx);
            continue;
        }
        edit.phi->set_operand(edit.incomingIdx - 1, edit.replacement);
        edit.phi->set_operand(edit.incomingIdx, pred);
    }
}

bool tryThreadEdge(BasicBlock *pred, BasicBlock *mid, const LoopInfo &LI,
                   const std::set<BasicBlock *> &loopHeaders) {
    if (!pred || !mid || pred->parent_ != mid->parent_)
        return false;
    if (!hasTerminatorEdge(pred, mid) || countTerminatorEdges(pred, mid) != 1)
        return false;

    ICmpInst *optionalCmp = nullptr;
    if (!isThreadableMidBlock(mid, optionalCmp))
        return false;

    auto *midBr = dynamic_cast<BranchInst *>(mid->get_terminator());
    BoolFactMap boolFacts;
    ICmpFactMap cmpFacts;
    collectEdgeFacts(pred, mid, boolFacts, cmpFacts);

    auto known = evaluateBoolWithSubstitution(midBr->get_operand(0), mid, pred,
                                              boolFacts, cmpFacts);
    if (!known.has_value()) return false;

    auto *chosenSucc = static_cast<BasicBlock *>(midBr->get_operand(*known ? 1 : 2));
    auto *otherSucc = static_cast<BasicBlock *>(midBr->get_operand(*known ? 2 : 1));
    (void)otherSucc;

    if (chosenSucc == pred || chosenSucc == mid) return false;
    if (isUnsafeLoopThread(pred, mid, chosenSucc, LI, loopHeaders))
        return false;

    std::vector<PhiRepairEdit> phiEdits;
    if (!planSuccessorPhiRepairs(chosenSucc, mid, pred, LI, phiEdits))
        return false;

    if (!redirectPredEdge(pred, mid, chosenSucc))
        return false;

    applySuccessorPhiRepairs(phiEdits, pred);
    removeIncomingFromPred(mid, pred);
    return true;
}

} // namespace

void JumpThreadingLite::execute(Module *module) {
    AnalysisManager AM;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func, &AM);
    }
}

PreservedAnalyses JumpThreadingLite::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, &AM);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool JumpThreadingLite::runOnModule(Module *module) {
    bool changed = false;
    AnalysisManager AM;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, &AM);
    }
    return changed;
}

bool JumpThreadingLite::runOnFunction(Function *func, AnalysisManager *AM) {
    // Each successful edge rewrite invalidates LoopInfo and restarts the
    // whole-CFG search.  Keep this fixed-point algorithm within a predictable
    // compile-time budget; earlier SCCP and CFGSimplify still process larger
    // functions.
    constexpr size_t kMaxThreadedBlocks = 512;
    if (func->basic_blocks_.size() > kMaxThreadedBlocks)
        return false;

    bool changedAny = false;
    bool changedThisRound = false;

    do {
        changedThisRound = false;

        LoopInfo localLI;
        LoopInfo *LI = &localLI;
        if (AM) {
            LI = &AM->getLoopInfo(func);
        } else {
            localLI.analyze(func);
        }

        std::set<BasicBlock *> loopHeaders;
        for (const auto &loop : LI->allLoops()) {
            if (loop->header)
                loopHeaders.insert(loop->header);
        }

        std::vector<BasicBlock *> mids(func->basic_blocks_.begin(),
                                       func->basic_blocks_.end());
        for (auto *mid : mids) {
            if (mid->parent_ != func) continue;
            std::vector<BasicBlock *> preds(mid->pre_bbs_.begin(), mid->pre_bbs_.end());
            for (auto *pred : preds) {
                if (pred->parent_ != func) continue;
                if (!tryThreadEdge(pred, mid, *LI, loopHeaders))
                    continue;

                changedAny = true;
                changedThisRound = true;
                if (AM)
                    AM->invalidateFunction(func, PreservedAnalyses::none());
                break;
            }
            if (changedThisRound)
                break;
        }
    } while (changedThisRound);

    return changedAny;
}
