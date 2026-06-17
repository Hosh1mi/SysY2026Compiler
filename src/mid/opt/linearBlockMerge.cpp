#include "../../include/mid/opt/linearBlockMerge.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <vector>

namespace {

void replacePhiPred(Function *func, BasicBlock *oldPred, BasicBlock *newPred) {
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi())
                break;
            auto *phi = static_cast<PhiInst *>(inst);
            for (unsigned i = 1; i < phi->num_ops_; i += 2) {
                if (phi->get_operand(i) == oldPred)
                    phi->set_operand(i, newPred);
            }
        }
    }
}

bool startsWithPhi(BasicBlock *bb) {
    return !bb->instr_list_.empty() && bb->instr_list_.front()->is_phi();
}

bool mergeIntoPredecessor(BasicBlock *bb) {
    auto *func = bb ? bb->parent_ : nullptr;
    if (!func)
        return false;

    auto *term = bb->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 1)
        return false;

    auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(0));
    if (!succ || succ == bb || succ->parent_ != func)
        return false;
    if (succ == func->basic_blocks_.front())
        return false;
    if (succ->pre_bbs_.size() != 1 || succ->pre_bbs_.front() != bb)
        return false;
    if (startsWithPhi(succ))
        return false;

    replacePhiPred(func, succ, bb);

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

} // namespace

void LinearBlockMerge::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LinearBlockMerge::execute(Module *module,
                                            AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LinearBlockMerge::runOnFunction(Function *func) {
    bool changed = false;
    bool localChanged = true;
    while (localChanged) {
        localChanged = false;
        std::vector<BasicBlock *> blocks(func->basic_blocks_.begin(),
                                         func->basic_blocks_.end());
        for (auto *bb : blocks) {
            if (bb->parent_ != func)
                continue;
            if (mergeIntoPredecessor(bb)) {
                changed = true;
                localChanged = true;
                break;
            }
        }
    }
    return changed;
}
