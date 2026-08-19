// 典型示例：
//   优化前：pred 已知 x == 0，却仍跳到 mid 再按 x == 0 选择 left/right。
//   优化后：pred 直接跳到 left，mid 留给事实尚未确定的其它前驱。
// 穿透前会先计算目标块 PHI 在新边上应接收的值。

#include "../../include/mid/opt/jumpThreadingLite.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/opt/branchFactUtils.hpp"

#include <optional>
#include <set>
#include <vector>

// 轻量跳转穿透根据前驱边上已知的分支事实，直接决定中间块的条件分支去向。
// 改写前会完整规划目标块 PHI 的新增、替换和删除操作；遇到循环头、回边或
// 无法唯一确定 PHI 值的路径时保守退出，从而保持 SSA 与循环结构有效。

namespace {

struct PhiRepairEdit {
    PhiInst *phi = nullptr;
    int incomingIdx = -1;       // 旧中间块入边对应的 block 操作数下标
    Value *replacement = nullptr;
    bool removeMidIncoming = false;
};

// 检查终结指令是否包含 from -> to 的 CFG 边。
bool hasTerminatorEdge(BasicBlock *from, BasicBlock *to) {
    auto *term = from ? from->get_terminator() : nullptr;
    if (!term) return false;
    for (unsigned i = 0; i < term->num_ops(); ++i) {
        if (term->get_operand(i) == to)
            return true;
    }
    return false;
}

// 统计终结指令指向同一目标的边数，兼容条件分支两臂同目标的情况。
int countTerminatorEdges(BasicBlock *from, BasicBlock *to) {
    auto *term = from ? from->get_terminator() : nullptr;
    if (!term) return 0;
    int count = 0;
    for (unsigned i = 0; i < term->num_ops(); ++i) {
        if (term->get_operand(i) == to)
            ++count;
    }
    return count;
}

// 比较两个值是否为同一 SSA 值或数值相同的整数常量。
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

// 拒绝会穿越循环头或制造回边的改写，保持 LoopInfo 所需的规范结构。
bool isUnsafeLoopThread(BasicBlock *pred, BasicBlock *mid, BasicBlock *chosenSucc,
                        const DominatorTreeAnalysis &DT,
                        const std::set<BasicBlock *> &loopHeaders) {
    if (isLoopHeader(mid, loopHeaders) || isLoopHeader(chosenSucc, loopHeaders))
        return true;

    // Mirroring LLVM's conservative loop-header stance: do not create a new
    // edge to a dominating block, which would be a new backedge candidate.
    if (DT.dominates(chosenSucc, pred))
        return true;

    // If the existing edge is already a backedge, keep loop structure intact.
    if (DT.dominates(mid, pred))
        return true;

    return false;
}

// 从后继块所有 PHI 中删除指定前驱对应的入值。
void removeIncomingFromPred(BasicBlock *succ, BasicBlock *pred) {
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (int i = static_cast<int>(phi->num_ops()) - 1; i >= 1; i -= 2) {
            if (phi->get_operand(i) == pred)
                phi->remove_operands(i - 1, i);
        }
    }
}

// 从 pred 到 succ 的分支方向提取布尔与整数比较事实。
void collectEdgeFacts(BasicBlock *pred, BasicBlock *succ, BoolFactMap &boolFacts,
                      ICmpFactMap &cmpFacts) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br || br->num_ops() != 3) return;
    auto *trueDest = static_cast<BasicBlock *>(br->get_operand(1));
    auto *falseDest = static_cast<BasicBlock *>(br->get_operand(2));
    if (trueDest != succ && falseDest != succ) return;
    recordAssumedBool(br->get_operand(0), trueDest == succ, boolFacts, cmpFacts);
}

// PHI 替代值必须在新前驱末尾可用，或属于可跨块复用的常量/参数/全局。
bool isAllowedPhiIncomingValue(Value *value, BasicBlock *pred,
                               const DominatorTreeAnalysis &DT) {
    if (dynamic_cast<Constant *>(value)) return true;
    if (dynamic_cast<Argument *>(value)) return true;
    if (dynamic_cast<GlobalVariable *>(value)) return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst || !inst->parent_) return false;
    if (inst->parent_->parent_ != pred->parent_) return false;
    return DT.dominates(inst->parent_, pred);
}

Value *substitutePhiOnEdge(Value *value, BasicBlock *mid, BasicBlock *pred,
                           std::set<PhiInst *> &visiting) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    if (!phi || phi->parent_ != mid) return value;
    if (!visiting.insert(phi).second) return nullptr;
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
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

// 将中间块 PHI 按给定前驱代入，再利用路径事实求出条件的确定布尔值。
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

// 验证中间块只含 PHI、可解释的比较和条件分支。
bool isThreadableMidBlock(BasicBlock *bb, ICmpInst *&cmpOut) {
    cmpOut = nullptr;
    auto *term = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!term || term->num_ops() != 3) return false;

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

// 将前驱终结指令中的 oldTarget 精确替换为 newTarget。
bool redirectPredEdge(BasicBlock *pred, BasicBlock *oldTarget,
                      BasicBlock *newTarget) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br) return false;

    if (br->num_ops() == 1) {
        if (br->get_operand(0) != oldTarget) return false;
        pred->remove_succ_basic_block(oldTarget);
        oldTarget->remove_pre_basic_block(pred);
        br->set_operand(0, newTarget);
        pred->add_succ_basic_block(newTarget);
        newTarget->add_pre_basic_block(pred);
        return true;
    }

    if (br->num_ops() != 3) return false;
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

// 在修改 CFG 前计算目标 PHI 的修复方案；任一入值无法确定时整次放弃。
bool planSuccessorPhiRepairs(BasicBlock *chosenSucc, BasicBlock *mid,
                             BasicBlock *pred, const DominatorTreeAnalysis &DT,
                             std::vector<PhiRepairEdit> &edits) {
    for (auto *inst : chosenSucc->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);

        int incomingIdx = -1;
        int existingPredIdx = -1;
        for (int i = 1; i < static_cast<int>(phi->num_ops()); i += 2) {
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
        if (!replacement || !isAllowedPhiIncomingValue(replacement, pred, DT))
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

// 原子应用预先验证的 PHI 修复方案。
void applySuccessorPhiRepairs(const std::vector<PhiRepairEdit> &edits,
                              BasicBlock *pred, bool midStillPredecessor) {
    for (const auto &edit : edits) {
        if (midStillPredecessor) {
            // Threading one of several incoming edges does not remove mid's
            // edge to the successor.  Preserve its original phi incoming and
            // add the newly direct predecessor (or do nothing when that
            // predecessor already had the same incoming value).
            if (!edit.removeMidIncoming)
                edit.phi->addIncoming(edit.replacement, pred);
            continue;
        }
        if (edit.removeMidIncoming) {
            edit.phi->remove_operands(edit.incomingIdx - 1, edit.incomingIdx);
            continue;
        }
        edit.phi->set_operand(edit.incomingIdx - 1, edit.replacement);
        edit.phi->set_operand(edit.incomingIdx, pred);
    }
}

// 尝试将单条 pred -> mid 边穿透到已确定的后继。
bool tryThreadEdge(BasicBlock *pred, BasicBlock *mid,
                   const DominatorTreeAnalysis &DT,
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
    if (isUnsafeLoopThread(pred, mid, chosenSucc, DT, loopHeaders))
        return false;

    // Bypassing mid substitutes its phis only in the selected successor's
    // phis.  A phi with any other external use would no longer dominate the
    // new direct edge, so leave such CFGs untouched.
    for (auto *inst : mid->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) break;
        for (const auto &use : phi->use_list_) {
            auto *user = use.user_;
            if (user == optionalCmp)
                continue;
            if (user && user->is_phi() && user->parent_ == chosenSucc)
                continue;
            return false;
        }
    }

    std::vector<PhiRepairEdit> phiEdits;
    if (!planSuccessorPhiRepairs(chosenSucc, mid, pred, DT, phiEdits))
        return false;

    const bool midStillPredecessor = mid->pre_bbs_.size() > 1;
    if (!redirectPredEdge(pred, mid, chosenSucc))
        return false;

    applySuccessorPhiRepairs(phiEdits, pred, midStillPredecessor);
    removeIncomingFromPred(mid, pred);
    if (!midStillPredecessor) {
        std::vector<BasicBlock *> oldSuccs(mid->succ_bbs_.begin(),
                                           mid->succ_bbs_.end());
        for (auto *succ : oldSuccs)
            removeIncomingFromPred(succ, mid);
        mid->parent_->remove_bb(mid);
    }
    return true;
}

} // namespace

// 兼容入口，使用局部 AnalysisManager 执行。
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

// 在单个函数内反复寻找可穿透边，直到没有新的 CFG 变化。
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
        DominatorTreeAnalysis localDT;
        LoopInfo *LI = &localLI;
        DominatorTreeAnalysis *DT = &localDT;
        if (AM) {
            LI = &AM->getLoopInfo(func);
            DT = &AM->getDominatorTree(func);
        } else {
            localDT.analyze(func);
            localLI.analyze(func, localDT);
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
                if (!tryThreadEdge(pred, mid, *DT, loopHeaders))
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
