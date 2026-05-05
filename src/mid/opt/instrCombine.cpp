#include "../../include/mid/opt/instrCombine.hpp"

void InstructionCombine::execute(Module *module) {
    for (auto &func : module->function_list_) {
        bool changed = true;
        while (changed) {
            changed = simplify(func);
        }
    }
}

bool InstructionCombine::simplify(Function *func) {
    bool changed = false;
    for (auto &bb : func->basic_blocks_) {
        for (auto &inst : bb->instr_list_) {
            // 处理二元运算
            if (inst->num_ops_ != 2) continue;
            Value *op0 = inst->get_operand(0);
            Value *op1 = inst->get_operand(1);
            auto op = inst->op_id_;

            // 加法/减法
            if (op == Instruction::Add || op == Instruction::FAdd) {
                // x + 0 = x
                if (dynamic_cast<ConstantInt*>(op1) && static_cast<ConstantInt*>(op1)->value_ == 0) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantInt*>(op0) && static_cast<ConstantInt*>(op0)->value_ == 0) {
                    inst->replace_all_use_with(op1); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op1) && static_cast<ConstantFloat*>(op1)->value_ == 0.0f) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op0) && static_cast<ConstantFloat*>(op0)->value_ == 0.0f) {
                    inst->replace_all_use_with(op1); changed = true;
                }
            }
            else if (op == Instruction::Sub || op == Instruction::FSub) {
                // x - 0 = x
                if (dynamic_cast<ConstantInt*>(op1) && static_cast<ConstantInt*>(op1)->value_ == 0) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op1) && static_cast<ConstantFloat*>(op1)->value_ == 0.0f) {
                    inst->replace_all_use_with(op0); changed = true;
                }
                // x - x = 0
                if (op0 == op1) {
                    Constant *zero = (op == Instruction::Sub) ? 
                        static_cast<Constant*>(new ConstantInt(inst->type_, 0)) :
                        static_cast<Constant*>(new ConstantFloat(inst->type_, 0.0));
                    inst->replace_all_use_with(zero); changed = true;
                }
            }
            // 乘法
            else if (op == Instruction::Mul || op == Instruction::FMul) {
                // x * 1 = x
                if (dynamic_cast<ConstantInt*>(op1) && static_cast<ConstantInt*>(op1)->value_ == 1) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantInt*>(op0) && static_cast<ConstantInt*>(op0)->value_ == 1) {
                    inst->replace_all_use_with(op1); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op1) && static_cast<ConstantFloat*>(op1)->value_ == 1.0f) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op0) && static_cast<ConstantFloat*>(op0)->value_ == 1.0f) {
                    inst->replace_all_use_with(op1); changed = true;
                }
                // x * 0 = 0
                if ((dynamic_cast<ConstantInt*>(op0) && static_cast<ConstantInt*>(op0)->value_ == 0) ||
                    (dynamic_cast<ConstantInt*>(op1) && static_cast<ConstantInt*>(op1)->value_ == 0) ||
                    (dynamic_cast<ConstantFloat*>(op0) && static_cast<ConstantFloat*>(op0)->value_ == 0.0f) ||
                    (dynamic_cast<ConstantFloat*>(op1) && static_cast<ConstantFloat*>(op1)->value_ == 0.0f)) {
                    Constant *zero = (op == Instruction::Mul) ? 
                        static_cast<Constant*>(new ConstantInt(inst->type_, 0)) :
                        static_cast<Constant*>(new ConstantFloat(inst->type_, 0.0));
                    inst->replace_all_use_with(zero); changed = true;
                }
            }
            // 除法
            else if (op == Instruction::SDiv || op == Instruction::FDiv) {
                // x / 1 = x
                if (dynamic_cast<ConstantInt*>(op1) && static_cast<ConstantInt*>(op1)->value_ == 1) {
                    inst->replace_all_use_with(op0); changed = true;
                } else if (dynamic_cast<ConstantFloat*>(op1) && static_cast<ConstantFloat*>(op1)->value_ == 1.0f) {
                    inst->replace_all_use_with(op0); changed = true;
                }
            }
        }
    }
    return changed;
}