#include "../../include/mid/opt/deadCodeEliminate.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <set>
#include <vector>

namespace {

Function *calledFunction(Instruction *inst) {
    auto *call = dynamic_cast<CallInst *>(inst);
    if (!call || call->num_ops_ == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
}

} // namespace

void DeadCodeEliminate::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses DeadCodeEliminate::execute(Module *module,
                                             AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool DeadCodeEliminate::runOnFunction(Function *func) {
    bool changed = false;
    bool localChanged = false;
    do {
        localChanged = false;
        localChanged |= eliminateTrivialPhis(func);
        localChanged |= eliminateDeadInstructions(func);
        changed |= localChanged;
    } while (localChanged);
    return changed;
}

bool DeadCodeEliminate::hasRequiredEffect(Instruction *inst) const {
    switch (inst->op_id_) {
        case Instruction::Ret:
        case Instruction::Br:
        case Instruction::Store:
            return true;
        case Instruction::Call: {
            Function *callee = calledFunction(inst);
            if (!callee)
                return true;
            return !callee->hasSemFlag(SemFlag::FnPure);
        }
        default:
            return false;
    }
}

bool DeadCodeEliminate::isInstructionDead(Instruction *inst) const {
    return inst && inst->use_list_.empty() && !hasRequiredEffect(inst);
}

bool DeadCodeEliminate::eliminateDeadInstructions(Function *func) {
    std::vector<Instruction *> worklist;
    std::set<Instruction *> queued;

    auto enqueueIfDead = [&](Instruction *inst) {
        if (isInstructionDead(inst) && queued.insert(inst).second)
            worklist.push_back(inst);
    };

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_)
            enqueueIfDead(inst);
    }

    bool changed = false;
    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();
        queued.erase(inst);

        BasicBlock *parent = inst->parent_;
        if (!parent || !isInstructionDead(inst))
            continue;

        std::vector<Instruction *> operands;
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            if (auto *opInst = dynamic_cast<Instruction *>(inst->get_operand(i)))
                operands.push_back(opInst);
        }

        if (!parent->delete_instr(inst))
            continue;
        changed = true;

        for (auto *opInst : operands)
            enqueueIfDead(opInst);
    }

    return changed;
}

bool DeadCodeEliminate::eliminateTrivialPhis(Function *func) {
    std::set<PhiInst *> worklist;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi())
                worklist.insert(static_cast<PhiInst *>(inst));
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        PhiInst *phi = *worklist.begin();
        worklist.erase(worklist.begin());
        if (!phi->parent_)
            continue;

        std::vector<PhiInst *> usersToRevisit;
        for (auto use : phi->use_list_) {
            auto *user = dynamic_cast<PhiInst *>(use.val_);
            if (user && user->parent_)
                usersToRevisit.push_back(user);
        }

        Value *common = nullptr;
        bool nonTrivial = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            Value *incoming = phi->get_operand(i);
            if (incoming == phi)
                continue;
            if (!common) {
                common = incoming;
            } else if (common != incoming) {
                nonTrivial = true;
                break;
            }
        }

        if (nonTrivial || !common)
            continue;

        phi->replace_all_use_with(common);
        BasicBlock *parent = phi->parent_;
        if (parent && parent->delete_instr(phi)) {
            changed = true;
            for (auto *user : usersToRevisit) {
                if (user->parent_)
                    worklist.insert(user);
            }
        }
    }
    return changed;
}
