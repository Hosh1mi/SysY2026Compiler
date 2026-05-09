#include "../../include/mid/opt/algebraSimplify.hpp"
#include <cassert>

void AlgebraSimplify::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration()) {
            runOnFunction(func);
        }
    }
}

void AlgebraSimplify::runOnFunction(Function *func) {
    for (auto bb : func->basic_blocks_) {
        runOnBasicBlock(bb);
    }
}

void AlgebraSimplify::runOnBasicBlock(BasicBlock *bb) {
    auto it = bb->instr_list_.begin();
    while (it != bb->instr_list_.end()) {
        Instruction *inst = *it;
        ++it;
        if (tryAlgebraicSimplification(inst)) {
            continue;
        }
        tryStrengthReduction(inst);
    }
}

Value* AlgebraSimplify::getConstantZero(Type *ty) {
    if (ty->tid_ == Type::FloatTyID) {
        return new ConstantFloat(ty, 0.0f);
    } else {
        return new ConstantInt(ty, 0);
    }
}

Value* AlgebraSimplify::getConstantOne(Type *ty) {
    if (ty->tid_ == Type::FloatTyID) {
        return new ConstantFloat(ty, 1.0f);
    } else {
        return new ConstantInt(ty, 1);
    }
}

Value* AlgebraSimplify::getConstantAllOnes(Type *ty) {
    if (ty->tid_ == Type::IntegerTyID) {
        auto int_ty = static_cast<IntegerType*>(ty);
        if (int_ty->num_bits_ == 1) {
            return new ConstantInt(ty, 1);
        } else {
            return new ConstantInt(ty, -1);
        }
    }
    return nullptr;
}

bool AlgebraSimplify::isPowerOfTwo(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

int AlgebraSimplify::log2Int(int v) {
    int r = 0;
    while (v >>= 1) ++r;
    return r;
}

// 辅助：判断常数是否为严格正的 2 的幂
static bool isPositivePowerOfTwoInt(ConstantInt *c) {
    if (!c) return false;
    int v = c->value_;
    return v > 0 && (v & (v - 1)) == 0;
}

bool AlgebraSimplify::tryAlgebraicSimplification(Instruction *inst) {
    if (!inst->is_binary()) return false;
    BinaryInst *bin = static_cast<BinaryInst*>(inst);
    Value *v1 = bin->get_operand(0);
    Value *v2 = bin->get_operand(1);
    Type *ty = inst->type_;

    auto *c1 = dynamic_cast<ConstantInt*>(v1);
    auto *c2 = dynamic_cast<ConstantInt*>(v2);
    auto *f1 = dynamic_cast<ConstantFloat*>(v1);
    auto *f2 = dynamic_cast<ConstantFloat*>(v2);

    // ---------- 常量折叠 ----------
    if (ty->tid_ == Type::IntegerTyID && c1 && c2) {
        int val1 = c1->value_, val2 = c2->value_;
        int result = 0;
        switch (inst->op_id_) {
            case Instruction::Add: result = val1 + val2; break;
            case Instruction::Sub: result = val1 - val2; break;
            case Instruction::Mul: result = val1 * val2; break;
            case Instruction::SDiv:
            case Instruction::UDiv:
                if (val2 == 0) return false;
                result = val1 / val2; break;
            case Instruction::SRem:
            case Instruction::URem:
                if (val2 == 0) return false;
                result = val1 % val2; break;
            case Instruction::And: result = val1 & val2; break;
            case Instruction::Or:  result = val1 | val2; break;
            case Instruction::Xor: result = val1 ^ val2; break;
            case Instruction::Shl: result = val1 << val2; break;
            case Instruction::LShr: // 右移：逻辑/算术均按移位处理
            case Instruction::AShr:
                result = static_cast<unsigned>(val1) >> val2; // 兼容性处理
                break;
            default: return false;
        }
        inst->replace_all_use_with(new ConstantInt(ty, result));
        inst->parent_->delete_instr(inst);
        return true;
    } else if (ty->tid_ == Type::FloatTyID && f1 && f2) {
        float fval1 = f1->value_, fval2 = f2->value_;
        float result = 0.0;
        switch (inst->op_id_) {
            case Instruction::FAdd: result = fval1 + fval2; break;
            case Instruction::FSub: result = fval1 - fval2; break;
            case Instruction::FMul: result = fval1 * fval2; break;
            case Instruction::FDiv:
                if (fval2 == 0.0f) return false;
                result = fval1 / fval2; break;
            default: return false;
        }
        inst->replace_all_use_with(new ConstantFloat(ty, result));
        inst->parent_->delete_instr(inst);
        return true;
    }

    // ---------- 常见代数恒等式 ----------
    if (ty->tid_ == Type::IntegerTyID) {
        int val1 = c1 ? c1->value_ : 0;
        int val2 = c2 ? c2->value_ : 0;

        switch (inst->op_id_) {
            // ---- 原有简化 ----
            case Instruction::Add:
                if (c2 && val2 == 0) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (c1 && val1 == 0) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                break;
            case Instruction::Sub:
                if (c2 && val2 == 0) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) { // sub x, x → 0
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::Mul:
                if (c2 && val2 == 1) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (c1 && val1 == 1) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                if ((c2 && val2 == 0) || (c1 && val1 == 0)) {
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::SDiv:
            case Instruction::UDiv:
                if (c2 && val2 == 1) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (c1 && val1 == 0) { inst->replace_all_use_with(getConstantZero(ty)); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) { // div x, x → 1
                    inst->replace_all_use_with(getConstantOne(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::SRem:
            case Instruction::URem:
                if (c2 && val2 == 1) { inst->replace_all_use_with(getConstantZero(ty)); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) { // rem x, x → 0
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::Shl:
            case Instruction::LShr:
            case Instruction::AShr:
                if (c2 && val2 == 0) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                break;
            case Instruction::And:
                if ((c2 && val2 == 0) || (c1 && val1 == 0)) {
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                if (c2 && val2 == static_cast<ConstantInt*>(getConstantAllOnes(ty))->value_) {
                    inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true;
                }
                if (c1 && val1 == static_cast<ConstantInt*>(getConstantAllOnes(ty))->value_) {
                    inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true;
                }
                if (v1 == v2) { // and x, x → x
                    inst->replace_all_use_with(v1);
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::Or:
                if (c2 && val2 == 0) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (c1 && val1 == 0) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                if ((c2 && val2 == static_cast<ConstantInt*>(getConstantAllOnes(ty))->value_) ||
                    (c1 && val1 == static_cast<ConstantInt*>(getConstantAllOnes(ty))->value_)) {
                    inst->replace_all_use_with(getConstantAllOnes(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                if (v1 == v2) { // or x, x → x
                    inst->replace_all_use_with(v1);
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::Xor:
                if (c2 && val2 == 0) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (c1 && val1 == 0) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) { // xor x, x → 0
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            default:
                break;
        }
    } else if (ty->tid_ == Type::FloatTyID) {
        float fval1 = f1 ? f1->value_ : 0.0f;
        float fval2 = f2 ? f2->value_ : 0.0f;

        switch (inst->op_id_) {
            case Instruction::FAdd:
                if (f2 && fval2 == 0.0f) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (f1 && fval1 == 0.0f) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                break;
            case Instruction::FSub:
                if (f2 && fval2 == 0.0f) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) {
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::FMul:
                if (f2 && fval2 == 1.0f) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (f1 && fval1 == 1.0f) { inst->replace_all_use_with(v2); inst->parent_->delete_instr(inst); return true; }
                if ((f2 && fval2 == 0.0f) || (f1 && fval1 == 0.0f)) {
                    inst->replace_all_use_with(getConstantZero(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            case Instruction::FDiv:
                if (f2 && fval2 == 1.0f) { inst->replace_all_use_with(v1); inst->parent_->delete_instr(inst); return true; }
                if (f1 && fval1 == 0.0f) { inst->replace_all_use_with(getConstantZero(ty)); inst->parent_->delete_instr(inst); return true; }
                if (v1 == v2) {
                    inst->replace_all_use_with(getConstantOne(ty));
                    inst->parent_->delete_instr(inst);
                    return true;
                }
                break;
            default:
                break;
        }
    }

    return false;
}

bool AlgebraSimplify::tryStrengthReduction(Instruction *inst) {
    // -------- 乘法强度削弱（原有）--------
    if (inst->op_id_ == Instruction::Mul && inst->type_->tid_ == Type::IntegerTyID) {
        Value *v1 = inst->get_operand(0);
        Value *v2 = inst->get_operand(1);
        ConstantInt *c1 = dynamic_cast<ConstantInt*>(v1);
        ConstantInt *c2 = dynamic_cast<ConstantInt*>(v2);

        Value *var = nullptr;
        int constant = 0;
        if (c1) { constant = c1->value_; var = v2; }
        else if (c2) { constant = c2->value_; var = v1; }
        else return false;

        if (constant <= 1 || constant > 16) return false;

        BasicBlock *bb = inst->parent_;
        Type *ty = var->type_;

        if (isPowerOfTwo(constant)) {
            int shift = log2Int(constant);
            auto *shl = new BinaryInst(ty, Instruction::Shl, var, new ConstantInt(ty, shift), bb, true);
            bb->add_instruction_before_inst(shl, inst);
            inst->replace_all_use_with(shl);
            bb->delete_instr(inst);
            return true;
        }

        std::vector<int> shifts;
        for (int i = 0; i < 32; ++i) {
            if (constant & (1 << i)) shifts.push_back(i);
        }
        if (shifts.size() > 3) return false;

        Value *sum = nullptr;
        for (int s : shifts) {
            Value *term = nullptr;
            if (s == 0) {
                term = var;
            } else {
                term = new BinaryInst(ty, Instruction::Shl, var, new ConstantInt(ty, s), bb, true);
                bb->add_instruction_before_inst(static_cast<Instruction*>(term), inst);
            }
            if (sum == nullptr) {
                sum = term;
            } else {
                sum = new BinaryInst(ty, Instruction::Add, sum, term, bb, true);
                bb->add_instruction_before_inst(static_cast<Instruction*>(sum), inst);
            }
        }
        inst->replace_all_use_with(sum);
        bb->delete_instr(inst);
        return true;
    }

    // -------- 除法/取模强度削弱（新增）--------
    if ((inst->op_id_ == Instruction::SDiv || inst->op_id_ == Instruction::SRem ||
         inst->op_id_ == Instruction::UDiv || inst->op_id_ == Instruction::URem) &&
        inst->type_->tid_ == Type::IntegerTyID) {
        
        Value *v1 = inst->get_operand(0);
        Value *v2 = inst->get_operand(1);
        auto *c = dynamic_cast<ConstantInt*>(v2);
        if (!c) return false;

        // 只处理除数为正的 2 的幂的情况
        if (!isPositivePowerOfTwoInt(c)) return false;

        int shift = log2Int(c->value_);
        BasicBlock *bb = inst->parent_;
        Type *ty = v1->type_;

        if (inst->op_id_ == Instruction::SDiv || inst->op_id_ == Instruction::UDiv) {
            // 除法 → 逻辑右移（对非负整数语义正确）
            auto *lshr = new BinaryInst(ty, Instruction::LShr, v1,
                                        new ConstantInt(ty, shift), bb, true);
            bb->add_instruction_before_inst(lshr, inst);
            inst->replace_all_use_with(lshr);
            bb->delete_instr(inst);
            return true;
        } else { // SRem / URem
            // 取模 → 按位与 (c-1)
            int mask = c->value_ - 1;
            auto *andInst = new BinaryInst(ty, Instruction::And, v1,
                                           new ConstantInt(ty, mask), bb, true);
            bb->add_instruction_before_inst(andInst, inst);
            inst->replace_all_use_with(andInst);
            bb->delete_instr(inst);
            return true;
        }
    }

    return false;
}