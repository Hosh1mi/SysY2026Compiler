// 典型示例：
//   优化前：br i1 true, label %then, label %else
//   优化后：br label %then
// 常量条件确定了唯一后继，未选分支及其 PHI 入边随后可被清理。

#include "../../include/mid/opt/CFGSimplify.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/dominanceAnalysis.hpp"
#include "../../include/mid/opt/cfgUtils.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// CFG 简化负责清理控制流图中的冗余结构，并在每次改写后同步维护 PHI 入边。
// 主要动作包括折叠常量分支、合并空块和线性块、外提循环不变量分支，以及
// 将收益明确的菱形分支改写为 select。各动作反复执行，直到本轮不再改变 CFG。

// 辅助函数：在 succ 的所有 phi 中删除 deadBlock 对应的入边
static void removeBBFromPhi(BasicBlock *deadBlock, BasicBlock *succ) {
    for (auto *instr : succ->instr_list_) {
        if (!instr->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(instr);
        // 从后向前扫描，避免索引偏移
        for (int i = phi->num_ops() - 1; i >= 0; i -= 2) {
            if (phi->get_operand(i) == deadBlock) {
                phi->remove_operands(i - 1, i);
            }
        }
    }
}

// 将基本块的结尾从条件分支替换为无条件跳转到 target
static void replaceBranchWithUncond(BasicBlock *bb, BasicBlock *target) {
    auto *oldBr = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!oldBr || oldBr->num_ops() != 3) return;

    auto *trueDest  = dynamic_cast<BasicBlock *>(oldBr->get_operand(1));
    auto *falseDest = dynamic_cast<BasicBlock *>(oldBr->get_operand(2));

    // 确定"被抛弃"的那个目标块（不再跳转到的块）
    BasicBlock *nonTarget = (target == trueDest) ? falseDest : trueDest;
    // 清理 nonTarget 中引用 bb 的 phi 入边（mem2reg 后 phi 节点中会有 bb 的入边）
    removeBBFromPhi(bb, nonTarget);

    // 移除两条旧边
    bb->remove_succ_basic_block(trueDest);
    bb->remove_succ_basic_block(falseDest);
    trueDest->remove_pre_basic_block(bb);
    falseDest->remove_pre_basic_block(bb);

    // 删除旧指令
    bb->delete_instr(oldBr);

    // 创建新无条件跳转（自动添加 CFG 边）
    new BranchInst(target, bb);
}

// 将一个无条件跳转的目标从 oldTarget 改为 newTarget，并维护 CFG 和 phi
static void redirectUncondBr(BasicBlock *pred, BasicBlock *oldTarget, BasicBlock *newTarget) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br || br->num_ops() != 1) return;

    // 更新 CFG
    pred->remove_succ_basic_block(oldTarget);
    oldTarget->remove_pre_basic_block(pred);
    br->set_operand(0, newTarget);
    pred->add_succ_basic_block(newTarget);
    newTarget->add_pre_basic_block(pred);
}

// 将一个条件分支的某个目标从 oldTarget 改为 newTarget
static void redirectCondBr(BasicBlock *pred, int idx, BasicBlock *oldTarget, BasicBlock *newTarget) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br || br->num_ops() != 3) return;

    pred->remove_succ_basic_block(oldTarget);
    oldTarget->remove_pre_basic_block(pred);
    br->set_operand(idx, newTarget);
    pred->add_succ_basic_block(newTarget);
    newTarget->add_pre_basic_block(pred);
}

// 将目标块中所有引用 deadBlock 的 phi 入边改为指向对应的前驱块列表
static void updatePhis(BasicBlock *target, BasicBlock *deadBlock,
                       const std::vector<BasicBlock *> &preds) {
    for (auto *instr : target->instr_list_) {
        if (!instr->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(instr);

        // 收集 deadBlock 对应的值
        std::vector<Value *> vals;
        // 从后向前扫描，避免索引偏移问题
        for (int i = phi->num_ops() - 1; i >= 0; i -= 2) {
            if (phi->get_operand(i) == deadBlock) {   // i 是基本块操作数
                vals.push_back(phi->get_operand(i - 1)); // 对应的值
                // 删除这一对操作数
                phi->remove_operands(i - 1, i);
            }
        }

        // 为每个前驱添加一条入边（值相同）
        // 注意：如果某个 pred 已经存在于 target 的 phi 中（即 pred 已经有一条
        // 直接分支到 target），再次添加会导致 phi 中出现同一个前驱的重复条目，
        // 且两值可能不同。这种情况下应跳过，因为原条目已包含正确的值。
        for (auto *pred : preds) {
            // 检查 pred 是否已经有一个 phi 条目
            bool alreadyHasEntry = false;
            for (int i = 0; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i + 1) == pred) {
                    alreadyHasEntry = true;
                    break;
                }
            }
            if (alreadyHasEntry) continue;

            for (auto *val : vals) {
                phi->addIncoming(val, pred);
            }
        }
    }
}

static BasicBlock *getEntryBlock(Function *func) {
    return func->basic_blocks_.empty() ? nullptr : func->basic_blocks_.front();
}

// Merge a block with its sole successor when the successor has no other
// predecessor. This is the non-speculative linear-block fold that belongs to
// basic CFG canonicalization.
static bool mergeLinearSuccessor(BasicBlock *bb) {
    auto *func = bb ? bb->parent_ : nullptr;
    if (!func)
        return false;

    auto *term = bb->get_terminator();
    if (!term || !term->is_br() || term->num_ops() != 1)
        return false;

    auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(0));
    if (!succ || succ == bb || succ->parent_ != func ||
        succ == getEntryBlock(func))
        return false;
    if (succ->pre_bbs_.size() != 1 || succ->pre_bbs_.front() != bb)
        return false;
    if (!succ->instr_list_.empty() && succ->instr_list_.front()->is_phi())
        return false;

    for (auto *next : succ->succ_bbs_) {
        for (auto *inst : next->instr_list_) {
            if (!inst->is_phi())
                break;
            auto *phi = static_cast<PhiInst *>(inst);
            for (unsigned i = 1; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i) == succ)
                    phi->set_operand(i, bb);
            }
        }
    }

    std::vector<BasicBlock *> newSuccs = succ->succ_bbs_;
    bb->delete_instr(term);

    std::vector<Instruction *> toMove(succ->instr_list_.begin(),
                                      succ->instr_list_.end());
    for (auto *inst : toMove) {
        succ->remove_instr(inst);
        bb->add_instruction(inst);
    }

    for (auto *next : newSuccs) {
        next->remove_pre_basic_block(succ);
        bb->add_succ_basic_block(next);
        next->add_pre_basic_block(bb);
    }
    func->remove_bb(succ);
    return true;
}

static bool mergeEmptyBlock(BasicBlock *bb) {
    // 入口块不合并
    if (bb == getEntryBlock(bb->parent_)) return false;

    // 检查块内是否只有一条指令（即终止指令本身）
    // 如果块内有 phi、add、load 等其他指令，则不是空块，不可合并
    if (bb->instr_list_.size() != 1) return false;

    auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!br || br->num_ops() != 1) return false;

    auto *target = dynamic_cast<BasicBlock *>(br->get_operand(0));
    if (target == bb) return false; // 自环

    // A predecessor may already branch directly to target.  The two logical
    // edges can be collapsed when every target PHI observes the same value;
    // otherwise merging would lose the edge distinction.
    for (auto *pred : bb->pre_bbs_) {
        if (std::find(target->pre_bbs_.begin(), target->pre_bbs_.end(), pred) != target->pre_bbs_.end()) {
            for (auto *instr : target->instr_list_) {
                if (!instr->is_phi()) break;
                auto *phi = static_cast<PhiInst *>(instr);
                Value *viaEmpty = nullptr;
                Value *direct = nullptr;
                for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                    if (phi->get_operand(i + 1) == bb)
                        viaEmpty = phi->get_operand(i);
                    else if (phi->get_operand(i + 1) == pred)
                        direct = phi->get_operand(i);
                }
                if (!viaEmpty || !direct || viaEmpty != direct)
                    return false;
            }
        }
    }

    // 如果该块没有前驱（如 TailRecursionEliminate 新创建的 preheader），
    // 且目标块有 phi 引用此块，则不应合并 —— 否则 phi 中来自此块的初始化条目
    // 会丢失（updatePhis 的 preds 为空，无法补回任何条目）。
    if (bb->pre_bbs_.empty()) {
        if (!target->instr_list_.empty()) {
            for (auto *instr : target->instr_list_) {
                if (!instr->is_phi()) break;
                auto *phi = static_cast<PhiInst *>(instr);
                for (int i = 0; i < phi->num_ops(); i += 2) {
                    if (phi->get_operand(i + 1) == bb)
                        return false; // phi 引用此无前驱块，不能合并
                }
            }
        }
        // 该块无前驱且目标没有引用它的 phi，可以直接删除
        //（它已经是死代码，不需要合并）
        bb->parent_->remove_bb(bb);
        return true;
    }

    // 如果目标块有 phi，则需将 deadBlock 对应的入边重定向到 preds
    if (!target->instr_list_.empty() && target->instr_list_.front()->is_phi()) {
        auto preds = bb->pre_bbs_; // 拷贝一份前驱列表
        updatePhis(target, bb, preds);
    }

    // 将所有前驱的分支目标从 bb 改为 target
    auto preds = bb->pre_bbs_; // 再拷贝一份（迭代过程中列表可能变化）
    for (auto *pred : preds) {
        auto *predTerm = pred->get_terminator();
        if (auto *predBr = dynamic_cast<BranchInst *>(predTerm)) {
            if (predBr->num_ops() == 1) {
                redirectUncondBr(pred, bb, target);
            } else if (predBr->num_ops() == 3) {
                bool redirected = false;
                for (int i = 1; i <= 2; ++i) {
                    if (predBr->get_operand(i) == bb) {
                        redirectCondBr(pred, i, bb, target);
                        redirected = true;
                    }
                }
                if (redirected &&
                    predBr->get_operand(1) == predBr->get_operand(2)) {
                    pred->delete_instr(predBr);
                    new BranchInst(target, pred);
                }
            }
        }
    }

    // 删除空基本块（此时它应已无前驱）
    if (bb->pre_bbs_.empty()) {
        std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
        for (auto *instr : instrs) {
            bb->delete_instr(instr); // 此时只有一条 br 指令，可安全删除
        }
        bb->parent_->remove_bb(bb);
        return true;
    }
    return false;
}

// 折叠常量条件分支
static bool foldConstantBranches(Function *func) {
    bool changed = false;
    for (auto *bb : func->basic_blocks_) {
        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br || br->num_ops() != 3) continue;

        auto *cond = br->get_operand(0);
        if (auto *ci = dynamic_cast<ConstantInt *>(cond)) {
            BasicBlock *target;
            if (ci->value_ != 0) {
                target = dynamic_cast<BasicBlock *>(br->get_operand(1));
            } else {
                target = dynamic_cast<BasicBlock *>(br->get_operand(2));
            }
            replaceBranchWithUncond(bb, target);
            changed = true;
        }
    }
    return changed;
}

static Value *getPhiIncomingValue(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

static bool phisHaveSameIncomingValues(BasicBlock *target,
                                       BasicBlock *lhsPred,
                                       BasicBlock *rhsPred) {
    for (auto *instr : target->instr_list_) {
        if (!instr->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instr);
        Value *lhs = getPhiIncomingValue(phi, lhsPred);
        Value *rhs = getPhiIncomingValue(phi, rhsPred);
        if (!lhs || !rhs || lhs != rhs) return false;
    }
    return true;
}

static void replacePhiPred(BasicBlock *target, BasicBlock *oldPred,
                           BasicBlock *newPred) {
    for (auto *instr : target->instr_list_) {
        if (!instr->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instr);
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) == oldPred)
                phi->set_operand(i + 1, newPred);
        }
    }
}

static bool blockHasOnlyOptionalCmpAndTerminator(BasicBlock *bb,
                                                 ICmpInst *cmp,
                                                 bool allowCmp) {
    for (auto *instr : bb->instr_list_) {
        if (instr->is_phi()) return false;
        if (instr->is_br()) return true;
        if (allowCmp && instr == cmp) continue;
        return false;
    }
    return false;
}

static bool branchTargetsBlock(BasicBlock *pred, BasicBlock *target) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br) return false;
    if (br->num_ops() == 1) return br->get_operand(0) == target;
    if (br->num_ops() == 3)
        return br->get_operand(1) == target || br->get_operand(2) == target;
    return false;
}

static void redirectBranchTarget(BasicBlock *pred, BasicBlock *oldTarget,
                                 BasicBlock *newTarget) {
    auto *br = dynamic_cast<BranchInst *>(pred->get_terminator());
    if (!br) return;
    if (br->num_ops() == 1) {
        redirectUncondBr(pred, oldTarget, newTarget);
        return;
    }
    if (br->num_ops() != 3) return;
    for (int i = 1; i <= 2; i++) {
        if (br->get_operand(i) == oldTarget) {
            redirectCondBr(pred, i, oldTarget, newTarget);
            return;
        }
    }
}

static bool valueDominatesBlock(Value *value, BasicBlock *bb,
                                const DominatorTreeAnalysis &DT) {
    if (dynamic_cast<Constant *>(value)) return true;
    if (dynamic_cast<GlobalVariable *>(value)) return true;
    if (dynamic_cast<Argument *>(value)) return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ && DT.dominates(inst->parent_, bb);
}

// Hoist loop-invariant branch conditions out of loop bodies.
// Detects the OR/AND short-circuit residue pattern where an invariant
// condition is re-checked on every iteration:
//
//   P: br i1 C1, label X, label B              (OR case: true→X, false→B)
//   B: br i1 C2, label X, label Y              (C2 is invariant relative to P)
//
// Transforms to:
//   guard: br i1 C2, label X, label P
//   P:    br i1 C1, label X, label Y            (B eliminated)
//
// The AND variant (P: true→B, B: false→Y) is handled symmetrically.
bool CFGSimplify::hoistLoopInvariantBranch(Function *func) {
    if (func->basic_blocks_.size() < 3) return false;

    bool changed = false;
    DominatorTreeAnalysis DT;
    DT.analyze(func);

    // Collect B blocks; we'll look at each once.
    auto bbs = func->basic_blocks_;
    for (auto *B : bbs) {
        // Skip blocks already removed from func in a previous iteration.
        if (std::find(func->basic_blocks_.begin(), func->basic_blocks_.end(), B)
            == func->basic_blocks_.end()) continue;
        if (B == func->basic_blocks_.front()) continue;
        if (B->pre_bbs_.size() != 1) continue;

        auto *P = B->pre_bbs_[0];
        if (!P || P->parent_ != func) continue;

        auto *bTerm = dynamic_cast<BranchInst *>(B->get_terminator());
        if (!bTerm || bTerm->num_ops() != 3) continue;

        auto *pTerm = dynamic_cast<BranchInst *>(P->get_terminator());
        if (!pTerm || pTerm->num_ops() != 3) continue;

        auto *pT = dynamic_cast<BasicBlock *>(pTerm->get_operand(1));
        auto *pF = dynamic_cast<BasicBlock *>(pTerm->get_operand(2));
        auto *bT = dynamic_cast<BasicBlock *>(bTerm->get_operand(1));
        auto *bF = dynamic_cast<BasicBlock *>(bTerm->get_operand(2));

        // Identify pattern: P and B share exactly one successor.
        // OR  pattern: P true→X, P false→B;  B true→X, B false→Y
        // AND pattern: P true→B, P false→Y;  B true→X, B false→Y
        BasicBlock *X = nullptr; // shared successor
        BasicBlock *Y = nullptr; // B's other successor (non-shared)
        bool isOr = false;

        if (pT == bT && pF == B) {
            X = pT; Y = bF; isOr = true;
        } else if (pF == bF && pT == B) {
            X = bT; Y = pF; isOr = false;
        } else {
            continue;
        }
        if (!X || !Y || X == Y || X == P || X == B || Y == P || Y == B)
            continue;

        BasicBlock *sharedSucc = isOr ? X : Y;
        BasicBlock *nonSharedSucc = isOr ? Y : X;
        if (!phisHaveSameIncomingValues(sharedSucc, P, B)) continue;

        Value *C2 = bTerm->get_operand(0);
        auto *cmpInst = dynamic_cast<ICmpInst *>(C2);
        if (!cmpInst) continue;

        BasicBlock *defBlock = cmpInst->parent_;
        if (defBlock == P || defBlock == B) continue;
        if (!DT.dominates(defBlock, P)) continue;
        if (!blockHasOnlyOptionalCmpAndTerminator(B, cmpInst, false)) continue;

        bool allInvariant = true;
        for (unsigned i = 0; i < cmpInst->num_ops(); i++) {
            if (!valueDominatesBlock(cmpInst->get_operand(i), P, DT)) {
                allInvariant = false;
                break;
            }
        }
        if (!allInvariant) continue;

        BasicBlock *entryPred = nullptr;
        int externalPreds = 0;
        for (auto *pred : P->pre_bbs_) {
            if (!DT.dominates(P, pred)) {
                entryPred = pred;
                externalPreds++;
            }
        }
        if (externalPreds != 1 || !entryPred) continue;
        if (!branchTargetsBlock(entryPred, P)) continue;

        // ── Apply transformation ──

        // Create guard block inserted before P.
        auto *guard = new BasicBlock(func->parent_, P->name_ + ".guard", func);
        auto itGuard = std::find(func->basic_blocks_.begin(),
                                 func->basic_blocks_.end(), guard);
        if (itGuard != func->basic_blocks_.end())
            func->basic_blocks_.erase(itGuard);
        auto itIns = std::find(func->basic_blocks_.begin(), func->basic_blocks_.end(), P);
        func->basic_blocks_.insert(itIns, guard);

        // Guard checks the invariant condition.
        if (isOr)
            new BranchInst(C2, X, P, guard);
        else
            new BranchInst(C2, P, Y, guard);

        // Redirect entryPred → guard instead of entryPred → P.
        redirectBranchTarget(entryPred, P, guard);

        // Update P's phis: entryPred entry → guard entry.
        replacePhiPred(P, entryPred, guard);

        // Redirect P's branch from B to Y.
        if (isOr)
            redirectCondBr(P, 2, B, Y);
        else
            redirectCondBr(P, 1, B, Y);

        // Shared successor can now be reached directly from guard.
        replacePhiPred(sharedSucc, B, guard);
        // B's non-shared successor is now reached from P.
        replacePhiPred(nonSharedSucc, B, P);

        // Remove CFG edges from B to its successors.
        X->remove_pre_basic_block(B);
        Y->remove_pre_basic_block(B);

        // Delete B.
        std::vector<Instruction *> instrs(B->instr_list_.begin(), B->instr_list_.end());
        for (auto *instr : instrs) B->delete_instr(instr);
        func->remove_bb(B);

        changed = true;
    }

    return changed;
}

// ── helpers for diamond→select conversion ──────────────────────────

static bool isSafeToSpeculate(Instruction *inst) {
    if (inst->is_load())  return false;
    if (inst->is_store()) return false;
    if (inst->is_call())  return false;
    if (inst->is_div())   return false;
    if (inst->is_rem())   return false;
    if (inst->is_ret())   return false;
    if (inst->is_br())    return false;
    return true;
}

static bool isCmpForSelect(Value *value) {
    return dynamic_cast<ICmpInst*>(value) || dynamic_cast<FCmpInst*>(value);
}

// Return all non-phi, non-terminator instructions in b.
static std::vector<Instruction*> getInstrs(BasicBlock *b) {
    std::vector<Instruction*> v;
    for (auto *i : b->instr_list_) {
        if (i->is_phi()) continue;
        if (i->isTerminator()) break;
        v.push_back(i);
    }
    return v;
}

static bool isCloneableForSelect(Instruction *inst) {
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<SelectInst *>(inst);
}

static bool canCloneBlock(const std::vector<Instruction *> &instrs) {
    std::unordered_set<Instruction *> available;
    for (auto *inst : instrs) {
        if (!isSafeToSpeculate(inst) || !isCloneableForSelect(inst))
            return false;
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            auto *def = dynamic_cast<Instruction *>(inst->get_operand(i));
            if (def && def->parent_ == inst->parent_ &&
                !available.count(def))
                return false;
        }
        available.insert(inst);
    }
    return true;
}

static int speculationCost(Instruction *inst) {
    switch (inst->op_id_) {
    case Instruction::Mul:
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
        return 3;
    case Instruction::Load:
        return 4;
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::FDiv:
        return 8;
    case Instruction::Select:
        // A select is not a free move: it consumes flags and extends a
        // dependent select chain.  Counting it like ordinary integer ALU
        // work makes an already if-converted inner diamond encourage
        // conversion of its enclosing guard as well.
        return 3;
    default:
        return 1;
    }
}

static bool isOrderedPredicate(ICmpInst::ICmpOp predicate) {
    switch (predicate) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_ULT:
    case ICmpInst::ICMP_ULE:
    case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE:
        return true;
    default:
        return false;
    }
}

static bool isMonotonicInductionCondition(ICmpInst *compare, BasicBlock *bb,
                                          const LoopInfo &loopInfo) {
    Loop *loop = loopInfo.getLoopFor(bb);
    PhiInst *iv = loop ? loop->getInductionIV() : nullptr;
    if (!compare || !loop || !iv || !isOrderedPredicate(compare->icmp_op_))
        return false;

    auto isInvariant = [&](Value *value) {
        auto *inst = dynamic_cast<Instruction *>(value);
        return !inst || !loop->isInLoop(inst);
    };
    if (compare->get_operand(0) == iv)
        return isInvariant(compare->get_operand(1));
    if (compare->get_operand(1) == iv)
        return isInvariant(compare->get_operand(0));
    return false;
}

static bool isProfitableDiamondToSelect(
    ICmpInst *compare, BasicBlock *bb,
    const std::vector<Instruction *> &trueInstrs,
    const std::vector<Instruction *> &falseInstrs, size_t selectCount,
    const LoopInfo &loopInfo) {
    int cost = static_cast<int>(selectCount);
    for (auto *inst : trueInstrs)
        cost += speculationCost(inst);
    for (auto *inst : falseInstrs)
        cost += speculationCost(inst);

    // A unit-step IV threshold changes direction at most once and is highly
    // predictable, so give it a smaller speculation budget than a branch
    // whose direction is not known structurally.
    bool monotonic = isMonotonicInductionCondition(compare, bb, loopInfo);
    constexpr int kMaxPredictableCost = 8;
    constexpr int kMaxUnpredictableCost = 12;
    bool profitable = cost <= (monotonic ? kMaxPredictableCost
                                         : kMaxUnpredictableCost);

    if (std::getenv("DEBUG_CFG_SELECT_COST")) {
        std::cerr << "[CFGSelectCost] block=" << bb->name_
                  << " cost=" << cost
                  << " monotonic=" << (monotonic ? "yes" : "no")
                  << " convert=" << (profitable ? "yes" : "no") << "\n";
    }
    return profitable;
}

// Clone inst into newBB with operand remapping via vm.
// Supports BinaryInst, ICmpInst, FCmpInst, SelectInst; returns nullptr otherwise.
static Instruction *cloneWithRemap(
    Instruction *inst, BasicBlock *newBB,
    const std::unordered_map<Value*, Value*> &vm)
{
    auto remap = [&](Value *v) -> Value* {
        auto it = vm.find(v);
        return it != vm.end() ? it->second : v;
    };
    if (auto *bin = dynamic_cast<BinaryInst*>(inst))
        return new BinaryInst(inst->type_, bin->op_id_,
            remap(bin->get_operand(0)), remap(bin->get_operand(1)), newBB, true);
    if (auto *cmp = dynamic_cast<ICmpInst*>(inst))
        return new ICmpInst(cmp->icmp_op_,
            remap(cmp->get_operand(0)), remap(cmp->get_operand(1)), newBB, true);
    if (auto *fcmp = dynamic_cast<FCmpInst*>(inst))
        return new FCmpInst(fcmp->fcmp_op_,
            remap(fcmp->get_operand(0)), remap(fcmp->get_operand(1)), newBB, true);
    if (auto *sel = dynamic_cast<SelectInst*>(inst)) {
        auto *clone = new SelectInst(remap(sel->get_operand(0)),
            remap(sel->get_operand(1)), remap(sel->get_operand(2)), sel->type_);
        clone->parent_ = newBB;
        return clone;
    }
    return nullptr;
}

// Speculatively clone all instructions of interBB into condBB before insertBefore.
// valMap is updated with original→clone mappings.
// Returns false if any instruction is unsafe or can't be cloned.
static bool tryCloneBlock(
    const std::vector<Instruction*>& instrs,
    BasicBlock *condBB,
    Instruction *insertBefore,
    std::unordered_map<Value*, Value*> &valMap)
{
    for (auto *inst : instrs) {
        if (!isSafeToSpeculate(inst)) return false;
        for (unsigned i = 0; i < inst->num_ops(); i++) {
            auto *def = dynamic_cast<Instruction*>(inst->get_operand(i));
            if (!def) continue;
            // Operand defined in same block must already have been cloned.
            if (def->parent_ == inst->parent_ && valMap.count(def) == 0)
                return false;
        }
        auto *clone = cloneWithRemap(inst, condBB, valMap);
        if (!clone) return false;
        valMap[inst] = clone;
        condBB->add_instruction_before_inst(clone, insertBefore);
    }
    return true;
}

// Convert if-else diamond patterns to select instructions.
//
// Three patterns, interBB may have 0–N safe-to-speculate instructions:
//   A) Both paths intermediate:  condBB → trueBB → mergeBB, condBB → falseBB → mergeBB
//   B) If-then:  condBB → trueBB → mergeBB (=falseBB)
//   C) If-else:  condBB → falseBB → mergeBB (=trueBB)
//
// The phi in mergeBB may have extra predecessors (&&/|| patterns with
// 3-predecessor phis); handled via in-place phi update in that case.
bool CFGSimplify::convertDiamondsToSelect(Function *func) {
    LoopInfo loopInfo;
    bool hasLoopInfo = false;

    for (auto *bb : func->basic_blocks_) {
        auto *term = bb->get_terminator();
        if (!term || !term->is_br() || term->num_ops() != 3) continue;

        auto *cmp = dynamic_cast<Instruction*>(term->get_operand(0));
        if (!cmp || !isCmpForSelect(cmp) || cmp->parent_ != bb) continue;
        auto it = std::find(bb->instr_list_.rbegin(), bb->instr_list_.rend(), term);
        if (it == bb->instr_list_.rend() || *++it != cmp) continue;
        if (cmp->use_list_.size() != 1) continue;

        auto *trueBB  = static_cast<BasicBlock*>(term->get_operand(1));
        auto *falseBB = static_cast<BasicBlock*>(term->get_operand(2));
        if (trueBB == falseBB) continue;

        // ── Determine structure ──
        auto *trueTerm  = trueBB->get_terminator();
        auto *falseTerm = falseBB->get_terminator();
        bool trueBr  = trueTerm  && trueTerm->is_br()  && trueTerm->num_ops()  == 1;
        bool falseBr = falseTerm && falseTerm->is_br() && falseTerm->num_ops() == 1;

        enum { PAT_A, PAT_B, PAT_C } pattern;
        BasicBlock *mergeBB      = nullptr;
        BasicBlock *interTrueBB  = nullptr;
        BasicBlock *interFalseBB = nullptr;

        if (trueBr && falseBr) {
            auto *tt = static_cast<BasicBlock*>(trueTerm->get_operand(0));
            auto *ft = static_cast<BasicBlock*>(falseTerm->get_operand(0));
            if (tt == ft) {
                // PAT_A: both converge to the same merge
                if (trueBB->pre_bbs_.size() != 1 || falseBB->pre_bbs_.size() != 1) continue;
                mergeBB = tt; pattern = PAT_A;
                interTrueBB = trueBB; interFalseBB = falseBB;
            } else if (tt == falseBB) {
                // trueBB → falseBB: trueBB is intermediate, falseBB is merge (PAT_B)
                if (trueBB->pre_bbs_.size() != 1) continue;
                mergeBB = falseBB; pattern = PAT_B; interTrueBB = trueBB;
            } else if (ft == trueBB) {
                // falseBB → trueBB: falseBB is intermediate, trueBB is merge (PAT_C)
                if (falseBB->pre_bbs_.size() != 1) continue;
                mergeBB = trueBB; pattern = PAT_C; interFalseBB = falseBB;
            } else {
                continue;
            }
        } else if (trueBr) {
            if (static_cast<BasicBlock*>(trueTerm->get_operand(0)) != falseBB) continue;
            if (trueBB->pre_bbs_.size() != 1) continue;
            mergeBB = falseBB; pattern = PAT_B; interTrueBB = trueBB;
        } else if (falseBr) {
            if (static_cast<BasicBlock*>(falseTerm->get_operand(0)) != trueBB) continue;
            if (falseBB->pre_bbs_.size() != 1) continue;
            mergeBB = trueBB; pattern = PAT_C; interFalseBB = falseBB;
        } else {
            continue;
        }

        // ── Find phis in mergeBB ──
        // mergeBB 的所有 phi 都必须能转成 select，否则任何一个被遗漏的
        // phi 在 inter 块删除后就会缺少来自 bb 的入边（错译）。
        std::vector<PhiInst*> phis;
        std::vector<Value*>   trueVals, falseVals;
        bool allConvertible = true;

        for (auto *i : mergeBB->instr_list_) {
            if (!i->is_phi()) break;
            auto *p = static_cast<PhiInst*>(i);
            Value *vT = nullptr, *vF = nullptr;
            for (unsigned k = 0; k < p->num_ops(); k += 2) {
                auto *pred = static_cast<BasicBlock*>(p->get_operand(k + 1));
                Value *val = p->get_operand(k);
                if (interTrueBB  && pred == interTrueBB)  vT = val;
                if (interFalseBB && pred == interFalseBB) vF = val;
                if (pattern == PAT_B && pred == bb) vF = val;
                if (pattern == PAT_C && pred == bb) vT = val;
            }
            if (!vT || !vF || p->num_ops() < 4) { allConvertible = false; break; }
            phis.push_back(p); trueVals.push_back(vT); falseVals.push_back(vF);
        }
        if (!allConvertible || phis.empty()) continue;

        // ── Clone intermediate blocks' instructions into condBB ──
        std::unordered_map<Value*, Value*> valMapT, valMapF;
        auto trueInstrs  = interTrueBB  ? getInstrs(interTrueBB)  : std::vector<Instruction*>{};
        auto falseInstrs = interFalseBB ? getInstrs(interFalseBB) : std::vector<Instruction*>{};

        if (!canCloneBlock(trueInstrs) || !canCloneBlock(falseInstrs))
            continue;
        if (!hasLoopInfo) {
            loopInfo.analyze(func);
            hasLoopInfo = true;
        }
        if (!isProfitableDiamondToSelect(
                dynamic_cast<ICmpInst *>(cmp), bb, trueInstrs, falseInstrs,
                phis.size(), loopInfo))
            continue;

        if (!tryCloneBlock(trueInstrs,  bb, term, valMapT)) continue;
        if (!tryCloneBlock(falseInstrs, bb, term, valMapF)) continue;

        // ── Create one select per phi ──
        std::vector<SelectInst*> sels;
        for (size_t pi = 0; pi < phis.size(); ++pi) {
            Value *trueVal  = trueVals[pi];
            Value *falseVal = falseVals[pi];
            Value *trueOperand  = valMapT.count(trueVal)  ? valMapT[trueVal]  : trueVal;
            Value *falseOperand = valMapF.count(falseVal) ? valMapF[falseVal] : falseVal;
            auto *sel = new SelectInst(cmp, trueOperand, falseOperand, trueOperand->type_);
            bb->add_instruction_before_inst(sel, term);
            sels.push_back(sel);
        }

        // ── Update phis ──
        for (size_t pi = 0; pi < phis.size(); ++pi) {
            PhiInst *phi = phis[pi];
            SelectInst *sel = sels[pi];
            if (phi->num_ops() == 4) {
                // 2-pred diamond: phi can be fully replaced by the select.
                // 替换后立即删除，避免留下入边集合与前驱不一致的僵尸 phi。
                phi->replace_all_use_with(sel);
                mergeBB->delete_instr(phi);
            } else {
                // Multi-pred phi (&&/|| pattern): update in place.
                for (auto *ib : {interTrueBB, interFalseBB}) {
                    if (!ib) continue;
                    for (int k = (int)phi->num_ops() - 1; k >= 0; k -= 2) {
                        if (phi->get_operand(k) == ib)
                            phi->remove_operands(k - 1, k);
                    }
                }
                if (pattern == PAT_B || pattern == PAT_C) {
                    for (int k = 1; k < (int)phi->num_ops(); k += 2) {
                        if (phi->get_operand(k) == bb) { phi->set_operand(k - 1, sel); break; }
                    }
                } else {
                    phi->addIncoming(sel, bb);
                }
            }
        }

        // ── Replace branch ──
        bb->delete_instr(term);
        for (auto *old : bb->succ_bbs_) old->remove_pre_basic_block(bb);
        bb->succ_bbs_.clear();
        for (auto *ib : {interTrueBB, interFalseBB}) {
            if (ib) mergeBB->remove_pre_basic_block(ib);
        }

        // Optionally sink mergeBB into condBB when it was the sole target.
        // 被 ret 引用的 phi 在替换后 ret 直接持有对应 select，
        // 因此用 ret 的实际操作数重建，而不是假定某个特定 select。
        auto *mt = mergeBB->get_terminator();
        bool sinkMerge = false;
        if (mt && mt->is_ret() && mt->num_ops() == 1 &&
            mergeBB != getEntryBlock(func) && mergeBB->pre_bbs_.empty()) {
            bool allPhisDead = true;
            for (auto *instr : mergeBB->instr_list_) {
                if (instr == mt) break;
                if (!instr->is_phi() || !instr->use_list_.empty()) { allPhisDead = false; break; }
            }
            if (allPhisDead) { new ReturnInst(mt->get_operand(0), bb); sinkMerge = true; }
        }
        if (!sinkMerge) { bb->succ_bbs_.push_back(mergeBB); new BranchInst(mergeBB, bb); }

        // LoopInfo and the block iterator are invalid after this CFG rewrite.
        // The caller repeats CFGSimplify to discover the next diamond.
        return true;
    }

    return false;
}

bool CFGSimplify::runOnModule(Module *module) {
    bool changedAny = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        bool funcChanged = false;

        // 0. hoistLoopInvariantBranch is intentionally not enabled here.
        // It is conservative internally, but short-circuit CFGs generated for
        // conv2d still need edge-splitting/phi-aware handling before this is
        // safe as a default CFGSimplify transform.

        // 1. diamond→select: clone + speculate, select creation
        // convertDiamondsToSelect(func);

        bool changed = true;
        while (changed) {
            changed = false;
            bool iterChanged = false;
            if (mode_ == CFGSimplifyMode::Full) {
                iterChanged |= convertDiamondsToSelect(func);
                // 2. 折叠常量分支
                iterChanged |= foldConstantBranches(func);

                // 3. 合并空基本块
                // 需要遍历副本，因为集合在遍历中可能被修改
                std::vector<BasicBlock *> bbs(
                    func->basic_blocks_.begin(), func->basic_blocks_.end());
                for (auto *bb : bbs) {
                    // 如果该块还存在且不是死块
                    if (bb->parent_ == func) {
                        iterChanged |= mergeEmptyBlock(bb);
                    }
                }

                // 4. 删除不可达块
                const size_t before = func->basic_blocks_.size();
                removeUnreachableBlocks(func);
                iterChanged |= before != func->basic_blocks_.size();
            }

            std::vector<BasicBlock *> bbs(func->basic_blocks_.begin(),
                                          func->basic_blocks_.end());
            for (auto *bb : bbs) {
                if (bb->parent_ == func && mergeLinearSuccessor(bb)) {
                    iterChanged = true;
                    break;
                }
            }

            changed = iterChanged;
            funcChanged |= iterChanged;
        }
        changedAny |= funcChanged;
    }
    return changedAny;
}

void CFGSimplify::execute(Module *module) {
    runOnModule(module);
}

PreservedAnalyses CFGSimplify::execute(Module *module, AnalysisManager &AM) {
    return runPass(module, AM).preserved;
}

PassRunResult CFGSimplify::runPass(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = runOnModule(module);
    return {changed, changed ? PreservedAnalyses::none()
                             : PreservedAnalyses::all()};
}
