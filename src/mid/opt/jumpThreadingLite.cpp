#include "../../include/mid/opt/jumpThreadingLite.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"

#include <vector>

namespace {

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

bool isAllowedPassthroughValue(Value *value, BasicBlock *pred) {
    if (dynamic_cast<Constant *>(value)) return true;
    if (dynamic_cast<Argument *>(value)) return true;
    if (dynamic_cast<GlobalVariable *>(value)) return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ == pred;
}

Value *substitutePhiOnEdge(Value *value, BasicBlock *mid, BasicBlock *pred) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    if (!phi || phi->parent_ != mid) return value;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) != pred) continue;
        Value *incoming = phi->get_operand(i);
        if (auto *incomingPhi = dynamic_cast<PhiInst *>(incoming)) {
            if (incomingPhi->parent_ == mid)
                return substitutePhiOnEdge(incomingPhi, mid, pred);
        }
        return incoming;
    }
    return nullptr;
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

bool repairSuccessorPhis(BasicBlock *chosenSucc, BasicBlock *mid,
                         BasicBlock *pred) {
    for (auto *inst : chosenSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);

        int incomingIdx = -1;
        for (int i = 1; i < static_cast<int>(phi->num_ops_); i += 2) {
            if (phi->get_operand(i) == mid) {
                incomingIdx = i;
                break;
            }
            if (phi->get_operand(i) == pred)
                return false;
        }
        if (incomingIdx < 0) continue;

        Value *replacement = substitutePhiOnEdge(phi->get_operand(incomingIdx - 1),
                                                 mid, pred);
        if (!replacement || !isAllowedPassthroughValue(replacement, pred))
            return false;
    }

    for (auto *inst : chosenSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = 1; i < static_cast<int>(phi->num_ops_); i += 2) {
            if (phi->get_operand(i) != mid) continue;
            Value *replacement = substitutePhiOnEdge(phi->get_operand(i - 1), mid, pred);
            phi->set_operand(i - 1, replacement);
            phi->set_operand(i, pred);
            break;
        }
    }
    return true;
}

bool tryThreadEdge(BasicBlock *pred, BasicBlock *mid) {
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
    if (!repairSuccessorPhis(chosenSucc, mid, pred))
        return false;

    if (!redirectPredEdge(pred, mid, chosenSucc))
        return false;

    removeIncomingFromPred(mid, pred);
    return true;
}

} // namespace

void JumpThreadingLite::execute(Module *module) {
    runOnModule(module);
}

PreservedAnalyses JumpThreadingLite::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    return runOnModule(module) ? PreservedAnalyses::none()
                               : PreservedAnalyses::all();
}

bool JumpThreadingLite::runOnModule(Module *module) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func);
    }
    return changed;
}

bool JumpThreadingLite::runOnFunction(Function *func) {
    bool changed = false;
    std::vector<BasicBlock *> mids(func->basic_blocks_.begin(), func->basic_blocks_.end());
    for (auto *mid : mids) {
        if (mid->parent_ != func) continue;
        std::vector<BasicBlock *> preds(mid->pre_bbs_.begin(), mid->pre_bbs_.end());
        for (auto *pred : preds) {
            if (pred->parent_ != func) continue;
            changed |= tryThreadEdge(pred, mid);
        }
    }
    return changed;
}
