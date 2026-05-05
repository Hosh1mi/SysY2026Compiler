#include "../../include/mid/opt/deadCodeDelete.hpp"

void DeadCodeDelete::execute(Module *module) {
    for (auto &func : module->function_list_) {
        bool changed = true;
        while (changed) {
            changed = removeDeadInstructions(func);
        }
    }
}

bool DeadCodeDelete::isCriticalInstruction(Instruction *inst) {
    // 有副作用的指令不能删除：store, call, ret, br 等
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

bool DeadCodeDelete::removeDeadInstructions(Function *func) {
    bool changed = false;
    for (auto &bb : func->basic_blocks_) {
        auto &instList = bb->instr_list_;
        for (auto it = instList.begin(); it != instList.end(); ) {
            Instruction *inst = *it;
            // 如果指令没有被任何其他指令使用，且不是关键指令
            if (inst->use_list_.empty() && !isCriticalInstruction(inst)) {
                it = instList.erase(it);
                inst->remove_use_of_ops(); // 清除对操作数的使用
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}