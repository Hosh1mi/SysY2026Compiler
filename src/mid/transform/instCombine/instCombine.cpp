#include "../../../include/mid/opt/instCombine.hpp"
#include "instCombineInternal.hpp"
#include <vector>

void InstCombine::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}

void InstCombine::runOnFunction(Function *func) {
    // Collect all instructions into the worklist
    std::vector<Instruction*> worklist;
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            worklist.push_back(inst);
        }
    }

    // Process until fixed point
    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();

        // Skip instructions that were deleted by an earlier iteration
        if (!inst->parent_) continue;

        // Skip terminators — they are never simplified here
        if (inst->isTerminator()) continue;

        Value *replacement = nullptr;

        switch (inst->op_id_) {
        case Instruction::Add:
            replacement = visitAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Sub:
            replacement = visitSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FAdd:
            replacement = visitFAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FSub:
            replacement = visitFSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FNeg:
            replacement = visitFNeg(static_cast<UnaryInst*>(inst));
            break;
        default:
            break;
        }

        if (replacement) {
            // Collect users *before* replace_all_use_with clears the use list
            std::vector<Instruction*> users;
            for (auto &use : inst->use_list_) {
                if (auto *user_inst = dynamic_cast<Instruction*>(use.val_)) {
                    users.push_back(user_inst);
                }
            }

            inst->replace_all_use_with(replacement);
            inst->parent_->delete_instr(inst);

            // Revisit users — they may now be simplifiable with the new operand
            for (auto *user : users) {
                worklist.push_back(user);
            }

            // If the replacement is itself an instruction, visit it too
            if (auto *new_inst = dynamic_cast<Instruction*>(replacement)) {
                worklist.push_back(new_inst);
            }
        }
    }
}
