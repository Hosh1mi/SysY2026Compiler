#include "../../include/mid/opt/CFGSimplify.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <vector>
#include <queue>

// 辅助函数：在 succ 的所有 phi 中删除 deadBlock 对应的入边
static void removeBBFromPhi(BasicBlock *deadBlock, BasicBlock *succ) {
    for (auto *instr : succ->instr_list_) {
        if (!instr->is_phi()) continue;
        auto *phi = static_cast<PhiInst *>(instr);
        // 从后向前扫描，避免索引偏移
        for (int i = phi->num_ops_ - 1; i >= 0; i -= 2) {
            if (phi->get_operand(i) == deadBlock) {
                phi->remove_operands(i - 1, i);
            }
        }
    }
}

// 将基本块的结尾从条件分支替换为无条件跳转到 target
static void replaceBranchWithUncond(BasicBlock *bb, BasicBlock *target) {
    auto *oldBr = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!oldBr || oldBr->num_ops_ != 3) return;

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
    if (!br || br->num_ops_ != 1) return;

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
    if (!br || br->num_ops_ != 3) return;

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
        for (int i = phi->num_ops_ - 1; i >= 0; i -= 2) {
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
            for (int i = 0; i < phi->num_ops_; i += 2) {
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

static bool mergeEmptyBlock(BasicBlock *bb) {
    // 入口块不合并
    if (bb == getEntryBlock(bb->parent_)) return false;

    // 检查块内是否只有一条指令（即终止指令本身）
    // 如果块内有 phi、add、load 等其他指令，则不是空块，不可合并
    if (bb->instr_list_.size() != 1) return false;

    auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
    if (!br || br->num_ops_ != 1) return false;

    auto *target = dynamic_cast<BasicBlock *>(br->get_operand(0));
    if (target == bb) return false; // 自环

    // 检查是否有前驱既是 bb 的前驱，又已经是 target 的前驱
    // 这种情况如果合并会导致 target 的 phi 中出现同一个前驱的重复条目
    // 且两个值可能不同，应避免合并以避免语义歧义
    for (auto *pred : bb->pre_bbs_) {
        if (std::find(target->pre_bbs_.begin(), target->pre_bbs_.end(), pred) != target->pre_bbs_.end()) {
            return false;
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
                for (int i = 0; i < phi->num_ops_; i += 2) {
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
            if (predBr->num_ops_ == 1) {
                redirectUncondBr(pred, bb, target);
            } else if (predBr->num_ops_ == 3) {
                for (int i = 1; i <= 2; ++i) {
                    if (predBr->get_operand(i) == bb) {
                        redirectCondBr(pred, i, bb, target);
                        break;
                    }
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

// 删除不可达块（无前驱的非入口块）
static void removeDeadBlocks(Function *func) {
    auto *entry = getEntryBlock(func);
    std::queue<BasicBlock *> worklist;
    for (auto *bb : func->basic_blocks_) {
        if (bb != entry && bb->pre_bbs_.empty()) {
            worklist.push(bb);
        }
    }

    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();

        // 修改后继块的前驱列表，并检查后继是否变成不可达
        auto succs = bb->succ_bbs_; // 拷贝
        for (auto *succ : succs) {
            // mem2reg 后，后继中可能有 phi 引用了 bb，需要清除这些入边
            removeBBFromPhi(bb, succ);

            succ->remove_pre_basic_block(bb);
            if (succ != entry && succ->pre_bbs_.empty()) {
                worklist.push(succ);
            }
        }

        // 删除该块及其指令
        std::vector<Instruction *> instrs(bb->instr_list_.begin(), bb->instr_list_.end());
        for (auto *instr : instrs) {
            bb->delete_instr(instr);
        }
        bb->parent_->remove_bb(bb);
    }
}

// 折叠常量条件分支
static bool foldConstantBranches(Function *func) {
    bool changed = false;
    for (auto *bb : func->basic_blocks_) {
        auto *br = dynamic_cast<BranchInst *>(bb->get_terminator());
        if (!br || br->num_ops_ != 3) continue;

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

// 主入口
void CFGSimplify::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;

        bool changed = true;
        while (changed) {
            changed = false;

            // 1. 折叠常量分支
            changed |= foldConstantBranches(func);

            // 2. 合并空基本块
            // 需要遍历副本，因为集合在遍历中可能被修改
            std::vector<BasicBlock *> bbs(func->basic_blocks_.begin(), func->basic_blocks_.end());
            for (auto *bb : bbs) {
                // 如果该块还存在且不是死块
                if (bb->parent_ == func) {
                    changed |= mergeEmptyBlock(bb);
                }
            }

            // 3. 删除不可达块
            removeDeadBlocks(func);
        }
    }
}
