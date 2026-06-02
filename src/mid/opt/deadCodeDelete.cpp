#include "../../include/mid/opt/deadCodeDelete.hpp"
#include <queue>
#include <set>
#include <vector>

// ------------------------------------------------------------
// execute：对每个函数反复执行优化，直到不再变化
void DeadCodeDelete::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration())
            continue;

        bool changed;
        do {
            changed = false;
            // changed |= removeDeadBlocks(func);
            changed |= removeDeadInstructions(func);
            // changed |= aggressiveDCE(func);
            changed |= simplifyCFG(func);
            changed |= eliminateTrivialPhis(func);
        } while (changed);
    }
}

// ------------------------------------------------------------
// 判断指令是否为“关键指令”（有副作用、控制流、内存写入、过程调用）
bool DeadCodeDelete::isCriticalInstruction(Instruction *inst) {
    switch (inst->op_id_) {
        case Instruction::Ret:    // 函数返回
        case Instruction::Br:     // 分支跳转
        case Instruction::Store:  // 内存写入
        case Instruction::Call:   // 过程调用
            return true;
        default:
            return false;
    }
}

static BasicBlock *getEntryBlock(Function *func) {
    for (auto bb : func->basic_blocks_) {
        if (bb->pre_bbs_.empty())
            return bb;
    }
    // fallback：理论上不会出现，但万一全都有前驱（比如循环结构错误），取第一个块
    return func->basic_blocks_.empty() ? nullptr : func->basic_blocks_.front();
}

// ------------------------------------------------------------
// 删除不可达的基本块
bool DeadCodeDelete::removeDeadBlocks(Function *func) {
    // 1. 计算可达块集合
    std::set<BasicBlock *> visited;
    std::queue<BasicBlock *> worklist;
    BasicBlock *entry = getEntryBlock(func);
    if (entry) {
        worklist.push(entry);
        visited.insert(entry);
    }

    while (!worklist.empty()) {
        BasicBlock *bb = worklist.front();
        worklist.pop();

        Instruction *terminator = bb->get_terminator();
        if (!terminator) continue;   // 无终结符的块不传播可达性

        // 遍历终结指令中所有是基本块类型的操作数（后继块）
        for (unsigned i = 0; i < terminator->num_ops_; ++i) {
            Value *op = terminator->get_operand(i);
            if (BasicBlock *succ = dynamic_cast<BasicBlock *>(op)) {
                if (visited.insert(succ).second) {
                    worklist.push(succ);
                }
            }
        }
    }

    // 2. 收集不可达块
    std::vector<BasicBlock *> to_remove;
    for (auto bb : func->basic_blocks_) {
        if (visited.find(bb) == visited.end()) {
            to_remove.push_back(bb);
        }
    }

    if (to_remove.empty())
        return false;

    // 3. 在删除前，更新所有含 Phi 指令的块，移除对即将删除块的引用
    for (auto bb : to_remove) {
        updatePhiAfterRemoveBlock(bb);
    }

    // 4. 真正删除块
    for (auto bb : to_remove) {
        deleteBasicBlock(bb);
    }

    return true;
}

// ------------------------------------------------------------
// 激进死代码消除（标记-清扫）
bool DeadCodeDelete::aggressiveDCE(Function *func) {
    std::set<Instruction *> live;
    markLiveInstructions(func, live);

    std::vector<Instruction *> dead_insts;
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (live.find(inst) == live.end() && !isCriticalInstruction(inst)) {
                dead_insts.push_back(inst);
            }
        }
    }

    for (auto inst : dead_insts) {
        if (inst->parent_) {
            inst->parent_->delete_instr(inst);  // 内部会清理 use 链
        }
    }

    return !dead_insts.empty();
}

// ------------------------------------------------------------
// 从关键指令出发，标记所有活跃指令
void DeadCodeDelete::markLiveInstructions(Function *func,
    std::set<Instruction *> &live) {
    std::queue<Instruction *> worklist;

    // 初始根集合：所有关键指令
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (isCriticalInstruction(inst)) {
                live.insert(inst);
                worklist.push(inst);
            }
        }
    }

    while (!worklist.empty()) {
        Instruction *cur = worklist.front();
        worklist.pop();

        for (unsigned i = 0; i < cur->num_ops_; ++i) {
            Value *op = cur->get_operand(i);
            if (Instruction *op_inst = dynamic_cast<Instruction *>(op)) {
                if (live.insert(op_inst).second) {
                    worklist.push(op_inst);
                }
            }
        }
    }
}

// ------------------------------------------------------------
// 控制流简化：合并仅包含无条件跳转且目标块只有一个前驱的块对
bool DeadCodeDelete::simplifyCFG(Function *func) {
    bool changed = false;
    bool local_changed = true;

    // 反复扫描直到稳定，因为合并可能产生新的可合并机会
    while (local_changed) {
        local_changed = false;
        for (auto bb : func->basic_blocks_) {
            Instruction *term = bb->get_terminator();
            // 只关心无条件跳转：br label %target
            if (!term || term->op_id_ != Instruction::Br || term->num_ops_ != 1)
                continue;

            BasicBlock *succ = static_cast<BasicBlock *>(term->get_operand(0));

            // 目标块必须有且仅有一个前驱（即当前块），且块首没有 Phi 指令
            if (succ->pre_bbs_.size() != 1 ||
                (!succ->instr_list_.empty() && succ->instr_list_.front()->is_phi()))
                continue;
    
            replacePhiUsesOfBlock(succ, bb);

            // 1. 删除当前块末尾的跳转指令
            bb->delete_instr(term);

            // 2. 将目标块中所有非终结指令移动到当前块末尾
            Instruction *succ_term = succ->get_terminator();
            for (auto it = succ->instr_list_.begin(); it != succ->instr_list_.end();) {
                Instruction *inst = *it;
                if (inst == succ_term) {
                    ++it;
                    continue;
                }
                // 从目标块移出（不清 use 链），然后插入当前块
                succ->remove_instr(inst);
                bb->add_instruction(inst);
                // 因为 remove_instr 可能使迭代器失效，重新获取列表头
                it = succ->instr_list_.begin();
            }

            // 3. 如果目标块有终结符，把它移动到当前块末尾
            if (succ_term) {
                succ->remove_instr(succ_term);
                bb->add_instruction(succ_term);
            }

            // 4. 更新 CFG：当前块的后继变为目标块的后继
            std::vector<BasicBlock *> new_succs = succ->succ_bbs_;  // 拷贝
            for (auto s : new_succs) {
                bb->add_succ_basic_block(s);
                s->remove_pre_basic_block(succ);
                s->add_pre_basic_block(bb);
            }

            // 5. 从函数中移除目标块（会调整前驱/后继关系）
            func->remove_bb(succ);
            delete succ;  // 释放内存，注意后续不再使用

            local_changed = true;
            changed = true;
            break;  // 向量被修改，跳出内层循环重新扫描
        }
    }
    return changed;
}

// ------------------------------------------------------------
// 删除一个基本块，并正确清理其内部指令的 def-use 链
void DeadCodeDelete::deleteBasicBlock(BasicBlock *bb) {
    for (auto inst : bb->instr_list_) {
        if (inst->type_->tid_ != Type::VoidTyID) {
            // 为不可达值生成一个零常量，替换所有使用
            // ConstantZero 是 IR 框架提供的通用零值，适用于各种类型
            auto *zero = new ConstantZero(inst->type_);
            inst->replace_all_use_with(zero);
        }
    }

    // 清除块内所有指令的 use 链（它们作为 user 的记录）
    for (auto inst : bb->instr_list_) {
        inst->remove_use_of_ops();
    }

    // 清空指令列表
    bb->instr_list_.clear();

    Function *func = bb->parent_;
    if (func) {
        func->remove_bb(bb);   // 从函数中移除，更新前驱后继关系
    }
    delete bb;
}

// ------------------------------------------------------------
// 更新所有基本块中的 Phi 指令，移除指向已删除块 bb 的输入
void DeadCodeDelete::updatePhiAfterRemoveBlock(BasicBlock *removed) {
    // 遍历 removed 的所有后继，这些后继中的 Phi 指令可能引用了 removed
    for (auto succ : removed->succ_bbs_) {
        for (auto &instr : succ->instr_list_) {
            if (!instr->is_phi())
                continue;
            PhiInst *phi = static_cast<PhiInst *>(instr);
            // 从后向前查找，避免删除时索引位移影响
            for (int i = phi->num_ops_ - 2; i >= 0; i -= 2) {
                // Phi 指令的格式：值, 前驱块, 值, 前驱块, ...
                if (phi->get_operand(i + 1) == removed) {
                    phi->remove_operands(i, i + 1);  // 删除一对 (value, bb)
                    break; // 一个 Phi 对同一个前驱最多只有一条记录
                }
            }
        }
    }
}

void DeadCodeDelete::replacePhiUsesOfBlock(BasicBlock *old_bb, BasicBlock *new_bb) {
    // 遍历函数中所有基本块
    Function *func = old_bb->parent_;
    if (!func) return;

    for (auto bb : func->basic_blocks_) {
        for (auto instr : bb->instr_list_) {
            if (!instr->is_phi())
                continue;
            PhiInst *phi = static_cast<PhiInst *>(instr);
            for (int i = 0; i < phi->num_ops_; i += 2) {
                // Phi 操作数：偶数索引是值，奇数索引是前驱块
                if (phi->get_operand(i + 1) == old_bb) {
                    phi->set_operand(i + 1, new_bb);  // 直接替换
                }
            }
        }
    }
}

// ------------------------------------------------------------
// 消除平凡 phi：所有非自引用操作数指向同一个值
bool DeadCodeDelete::eliminateTrivialPhis(Function *func) {
    std::set<PhiInst *> worklist;
    for (auto bb : func->basic_blocks_) {
        for (auto &inst : bb->instr_list_) {
            if (inst->is_phi())
                worklist.insert(static_cast<PhiInst *>(inst));
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        auto it = worklist.begin();
        auto phi = *it;
        worklist.erase(it);

        if (phi->parent_ == nullptr) continue;  // already deleted

        // Collect phi users for potential re-evaluation
        std::vector<PhiInst *> phiUsers;
        for (auto use : phi->use_list_) {
            auto *user = dynamic_cast<PhiInst *>(use.val_);
            if (user && user->parent_ != nullptr)
                phiUsers.push_back(user);
        }

        // Check if all non-self operands are the same value
        Value *common = nullptr;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            Value *v = phi->get_operand(i);
            if (v == phi) continue;  // skip self-reference
            if (common == nullptr) {
                common = v;
            } else if (common != v) {
                common = nullptr;
                break;
            }
        }

        if (common != nullptr) {
            phi->replace_all_use_with(common);
            phi->parent_->delete_instr(phi);
            changed = true;
            // Re-check phi users — they may have become trivial
            for (auto *user : phiUsers) {
                if (user->parent_ != nullptr)
                    worklist.insert(user);
            }
        }
    }
    return changed;
}

// ------------------------------------------------------------
// archived, but this works!!!
bool DeadCodeDelete::removeDeadInstructions(Function *func) {
    bool changed = false;
    for (auto bb : func->basic_blocks_) {
        auto &instList = bb->instr_list_;
        for (auto it = instList.begin(); it != instList.end();) {
            Instruction *inst = *it;
            if (inst->use_list_.empty() && !isCriticalInstruction(inst)) {
                it = instList.erase(it);
                inst->remove_use_of_ops();
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}