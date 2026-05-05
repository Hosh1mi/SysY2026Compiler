#include "../../include/mid/opt/constSpread.hpp"

void ConstSpread::execute(Module *module) {
    for (auto &func : module->function_list_) {
        bool changed = true;
        while (changed) {
            changed = foldConstants(func);
        }
    }
}

Constant *ConstSpread::computeConstOp(Instruction::OpID op, Constant *lhs, Constant *rhs, Type *ty) {
    // 只处理 int32 和 float 常量
    if (ty->tid_ == Type::IntegerTyID) {
        int lv = static_cast<ConstantInt*>(lhs)->value_;
        int rv = static_cast<ConstantInt*>(rhs)->value_;
        int res = 0;
        switch (op) {
            case Instruction::Add: res = lv + rv; break;
            case Instruction::Sub: res = lv - rv; break;
            case Instruction::Mul: res = lv * rv; break;
            case Instruction::SDiv: if (rv != 0) res = lv / rv; else return nullptr; break;
            case Instruction::SRem: if (rv != 0) res = lv % rv; else return nullptr; break;
            default: return nullptr;
        }
        return new ConstantInt(ty, res);
    } else if (ty->tid_ == Type::FloatTyID) {
        float lv = static_cast<ConstantFloat*>(lhs)->value_;
        float rv = static_cast<ConstantFloat*>(rhs)->value_;
        float res = 0.0f;
        switch (op) {
            case Instruction::FAdd: res = lv + rv; break;
            case Instruction::FSub: res = lv - rv; break;
            case Instruction::FMul: res = lv * rv; break;
            case Instruction::FDiv: if (rv != 0.0f) res = lv / rv; else return nullptr; break;
            default: return nullptr;
        }
        return new ConstantFloat(ty, res);
    }
    return nullptr;
}

bool ConstSpread::foldConstants(Function *func) {
    bool changed = false;
    for (auto &bb : func->basic_blocks_) {
        for (auto &inst : bb->instr_list_) {
            // 处理二元运算指令
            if (inst->num_ops_ == 2 && 
                (inst->op_id_ == Instruction::Add || inst->op_id_ == Instruction::Sub ||
                 inst->op_id_ == Instruction::Mul || inst->op_id_ == Instruction::SDiv ||
                 inst->op_id_ == Instruction::SRem || inst->op_id_ == Instruction::FAdd ||
                 inst->op_id_ == Instruction::FSub || inst->op_id_ == Instruction::FMul ||
                 inst->op_id_ == Instruction::FDiv)) {
                Value *op0 = inst->get_operand(0);
                Value *op1 = inst->get_operand(1);
                if (dynamic_cast<Constant*>(op0) && dynamic_cast<Constant*>(op1)) {
                    Constant *c0 = static_cast<Constant*>(op0);
                    Constant *c1 = static_cast<Constant*>(op1);
                    Constant *res = computeConstOp(inst->op_id_, c0, c1, inst->type_);
                    if (res) {
                        inst->replace_all_use_with(res);
                        // 标记当前指令为死代码（等待 DCE 删除）
                        changed = true;
                    }
                }
            }
        }
    }
    return changed;
}