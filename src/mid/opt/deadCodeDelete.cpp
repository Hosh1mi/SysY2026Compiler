#include "../../include/mid/opt/deadCodeDelete.hpp"
#include "../../include/mid/opt/cfgUtils.hpp"

#include <set>
#include <vector>

void DeadCodeDelete::execute(Module *module) {
    runOnModule(module);
}

PreservedAnalyses DeadCodeDelete::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    return runOnModule(module) ? PreservedAnalyses::none()
                               : PreservedAnalyses::all();
}

bool DeadCodeDelete::runOnModule(Module *module) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func);
    }
    return changed;
}

bool DeadCodeDelete::runOnFunction(Function *func) {
    bool changed = false;
    bool localChanged = true;
    while (localChanged) {
        localChanged = false;
        localChanged |= removeUnreachable(func);
        localChanged |= removeDeadInstructions(func);
        localChanged |= aggressiveDCE(func);
        localChanged |= simplifyCFG(func);
        localChanged |= eliminateTrivialPhis(func);
        changed |= localChanged;
    }
    return changed;
}

bool DeadCodeDelete::removeUnreachable(Function *func) {
    const size_t before = func->basic_blocks_.size();
    removeUnreachableBlocks(func);
    return before != func->basic_blocks_.size();
}

bool DeadCodeDelete::isCriticalInstruction(Instruction *inst) {
    switch (inst->op_id_) {
        case Instruction::Ret:
        case Instruction::Br:
        case Instruction::Store:
        case Instruction::Call:
            return true;
        default:
            return false;
    }
}

bool DeadCodeDelete::aggressiveDCE(Function *func) {
    std::set<Instruction *> live;
    markLiveInstructions(func, live);

    std::vector<Instruction *> deadInsts;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!live.count(inst) && !isCriticalInstruction(inst))
                deadInsts.push_back(inst);
        }
    }

    for (auto *inst : deadInsts) {
        if (inst->parent_)
            inst->parent_->delete_instr(inst);
    }
    return !deadInsts.empty();
}

void DeadCodeDelete::markLiveInstructions(Function *func,
                                          std::set<Instruction *> &live) {
    std::vector<Instruction *> worklist;

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!isCriticalInstruction(inst)) continue;
            if (live.insert(inst).second)
                worklist.push_back(inst);
        }
    }

    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            auto *opInst = dynamic_cast<Instruction *>(inst->get_operand(i));
            if (!opInst) continue;
            if (live.insert(opInst).second)
                worklist.push_back(opInst);
        }
    }
}

bool DeadCodeDelete::simplifyCFG(Function *func) {
    bool changed = false;
    bool localChanged = true;

    while (localChanged) {
        localChanged = false;
        std::vector<BasicBlock *> bbs(func->basic_blocks_.begin(),
                                      func->basic_blocks_.end());
        for (auto *bb : bbs) {
            if (bb->parent_ != func) continue;
            auto *term = bb->get_terminator();
            if (!term || term->op_id_ != Instruction::Br || term->num_ops_ != 1)
                continue;

            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(0));
            if (!succ || succ == bb || succ->parent_ != func) continue;
            if (succ->pre_bbs_.size() != 1) continue;
            if (!succ->instr_list_.empty() && succ->instr_list_.front()->is_phi())
                continue;

            replacePhiUsesOfBlock(succ, bb);

            bb->delete_instr(term);
            Instruction *succTerm = succ->get_terminator();
            std::vector<Instruction *> moved(succ->instr_list_.begin(),
                                             succ->instr_list_.end());
            for (auto *inst : moved) {
                succ->remove_instr(inst);
                bb->add_instruction(inst);
            }

            std::vector<BasicBlock *> succSuccs = succ->succ_bbs_;
            for (auto *succSucc : succSuccs) {
                succSucc->remove_pre_basic_block(succ);
                succSucc->add_pre_basic_block(bb);
                bb->add_succ_basic_block(succSucc);
            }

            func->remove_bb(succ);
            delete succ;

            localChanged = true;
            changed = true;
            break;
        }
    }

    return changed;
}

void DeadCodeDelete::replacePhiUsesOfBlock(BasicBlock *oldBb, BasicBlock *newBb) {
    Function *func = oldBb->parent_;
    if (!func) return;

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) continue;
            auto *phi = static_cast<PhiInst *>(inst);
            for (int i = 0; i < static_cast<int>(phi->num_ops_); i += 2) {
                if (phi->get_operand(i + 1) == oldBb)
                    phi->set_operand(i + 1, newBb);
            }
        }
    }
}

bool DeadCodeDelete::eliminateTrivialPhis(Function *func) {
    std::set<PhiInst *> worklist;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi())
                worklist.insert(static_cast<PhiInst *>(inst));
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        auto it = worklist.begin();
        auto *phi = *it;
        worklist.erase(it);
        if (phi->parent_ == nullptr) continue;

        std::vector<PhiInst *> phiUsers;
        for (auto use : phi->use_list_) {
            auto *user = dynamic_cast<PhiInst *>(use.val_);
            if (user && user->parent_ != nullptr)
                phiUsers.push_back(user);
        }

        Value *common = nullptr;
        bool first = true;
        bool conflict = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            Value *value = phi->get_operand(i);
            if (value == phi) continue;
            if (first) {
                common = value;
                first = false;
                continue;
            }
            if (common != value) {
                conflict = true;
                break;
            }
        }

        if (!first && !conflict) {
            phi->replace_all_use_with(common);
            phi->parent_->delete_instr(phi);
            changed = true;
            for (auto *user : phiUsers) {
                if (user->parent_ != nullptr)
                    worklist.insert(user);
            }
        }
    }

    return changed;
}

bool DeadCodeDelete::removeDeadInstructions(Function *func) {
    bool changed = false;
    for (auto *bb : func->basic_blocks_) {
        auto &instList = bb->instr_list_;
        for (auto it = instList.begin(); it != instList.end();) {
            Instruction *inst = *it;
            if (inst->use_list_.empty() && !isCriticalInstruction(inst)) {
                it = instList.erase(it);
                inst->remove_use_of_ops();
                inst->pos_in_bb.clear();
                inst->parent_ = nullptr;
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}
