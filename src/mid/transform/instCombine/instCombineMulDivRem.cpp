#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitMul  —  integer Mul simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitMul(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 * C2 → C3
    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    // 2. Canonicalize: constant to RHS  (mul C, x → mul x, C)
    if (cx && !cy) {
        if (inst->get_operand(1) == x)
            return nullptr;
        auto *new_inst = new BinaryInst(ty, Instruction::Mul, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // After canonicalization, any constant is on the RHS.

    // 3. Identity: x * 1 → x
    if (cy && cy->value_ == 1) {
        return x;
    }

    // 4. Zero: x * 0 → 0
    if (cy && cy->value_ == 0) {
        return make_const_int(ty, 0);
    }

    // 5. Reassociation: (mul x, C1) * C2 → mul x, C1*C2
    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_mul()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                int combined;
                if (!ConstantEvaluator::foldIntegerBinary(
                        Instruction::Mul, c1->value_, cy->value_, combined))
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Mul,
                    x_inst->get_operand(0),
                    make_const_int(ty, combined), bb, true);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    if (!cy) return nullptr;

    // 6. Fold  mul x, 2^k  whose sole user is  add/sub  with the same x.
    //    add x, (mul x, 2^k)   →  mul x, 2^k + 1
    //    sub (mul x, 2^k), x   →  mul x, 2^k - 1
    //    Done *before* mul→shl so the backend sees a single mul and can
    //    emit one fused instruction (e.g. add w0, w0, w0, lsl #k).
    if (isPowerOfTwo(cy->value_) && cy->value_ > 1 &&
        inst->use_list_.size() == 1) {
        auto *user = dynamic_cast<Instruction*>((*inst->use_list_.begin()).val_);
        if (user) {
            if (user->op_id_ == Instruction::Add) {
                Value *op0 = user->get_operand(0);
                Value *op1 = user->get_operand(1);
                if ((op0 == x && op1 == inst) || (op0 == inst && op1 == x)) {
                    auto *new_mul = new BinaryInst(ty, Instruction::Mul,
                        x, make_const_int(ty, cy->value_ + 1), bb, true);
                    stampIntegerFacts(new_mul);
                    bb->add_instruction_before_inst(new_mul, user);
                    user->replace_all_use_with(new_mul);
                    user->parent_->delete_instr(user);
                    bb->delete_instr(inst);
                    return nullptr;  // inst already deleted above
                }
            } else if (user->op_id_ == Instruction::Sub) {
                // sub (mul x, 2^k), x
                if (user->get_operand(0) == inst && user->get_operand(1) == x) {
                    auto *new_mul = new BinaryInst(ty, Instruction::Mul,
                        x, make_const_int(ty, cy->value_ - 1), bb, true);
                    stampIntegerFacts(new_mul);
                    bb->add_instruction_before_inst(new_mul, user);
                    user->replace_all_use_with(new_mul);
                    user->parent_->delete_instr(user);
                    bb->delete_instr(inst);
                    return nullptr;
                }
            }
        }
    }

    // 7. Strength reduction: mul x, 2^k  →  shl x, k
    //    Only reached when no add/sub fusion fired (rule 6).
    if (isPowerOfTwo(cy->value_) && cy->value_ > 1) {
        int shift = log2Int(cy->value_);
        auto *shl = new BinaryInst(ty, Instruction::Shl, x,
            make_const_int(ty, shift), bb, true);
        stampIntegerFacts(shl);
        bb->add_instruction_before_inst(shl, inst);
        return shl;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSDiv  —  integer SDiv simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSDiv(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 / C2 → C3  (guard division by zero)
    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    // 2. Identity: x / 1 → x
    if (cy && cy->value_ == 1) {
        return x;
    }

    // 3. x / -1  →  sub 0, x
    if (cy && cy->value_ == -1) {
        auto *neg = new BinaryInst(ty, Instruction::Sub,
            make_const_int(ty, 0), x, bb, true);
        stampIntegerFacts(neg);
        bb->add_instruction_before_inst(neg, inst);
        return neg;
    }

    // 4. sdiv x, 2^k  →  ashr x, k   (constant power-of-2 divisor)
    //    Exact when x ≥ 0 (no sign issue) or x is a multiple of 2^k
    //    (no remainder to lose — ashr matches sdiv even for negative x).
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        int k = log2Int(cy->value_);
        bool exact = isKnownMultipleOf(x, k, bb);
        if (isKnownNonNegative(x) || exact) {
            auto *ashr = new BinaryInst(ty, Instruction::AShr, x,
                            make_const_int(ty, k), bb, true);
            if (exact) ashr->setSemFlag(SemFlag::Exact);
            stampIntegerFacts(ashr);
            bb->add_instruction_before_inst(ashr, inst);
            return ashr;
        }
    }

    // 5. sdiv x, pow2_var  →  ashr x, k   (variable divisor proven = 2^k)
    if (!cy) {
        int k;
        if (isKnownPowerOfTwo(y, k) && k > 0) {
            bool exact = isKnownMultipleOf(x, k, bb);
            if (isKnownNonNegative(x) || exact) {
                auto *ashr = new BinaryInst(ty, Instruction::AShr, x,
                                make_const_int(ty, k), bb, true);
                if (exact) ashr->setSemFlag(SemFlag::Exact);
                stampIntegerFacts(ashr);
                bb->add_instruction_before_inst(ashr, inst);
                return ashr;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSRem  —  integer SRem simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSRem(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 % C2 → C3  (guard division by zero)
    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    // 2. Identity: x % 1 → 0
    if (cy && cy->value_ == 1) {
        return make_const_int(ty, 0);
    }

    // 3. srem x, 2^k  →  and x, 2^k-1
    //
    // (a) When the sole user is  icmp eq/ne …, 0  the transform is always
    //     safe: we only care about zero / non-zero, and  (x & mask) == 0
    //     iff  srem(x, 2^k) == 0  for all x (positive or negative).
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        if (inst->use_list_.size() == 1) {
            auto *cmp = dynamic_cast<ICmpInst*>((*inst->use_list_.begin()).val_);
            if (cmp && cmp->op_id_ == Instruction::ICmp &&
                (cmp->icmp_op_ == ICmpInst::ICMP_EQ ||
                 cmp->icmp_op_ == ICmpInst::ICMP_NE)) {
                auto *c0 = dynamic_cast<ConstantInt*>(cmp->get_operand(0));
                auto *c1 = dynamic_cast<ConstantInt*>(cmp->get_operand(1));
                bool cmp_with_zero = (c0 && c0->value_ == 0) || (c1 && c1->value_ == 0);
                if (cmp_with_zero) {
                    auto *andInst = new BinaryInst(ty, Instruction::And,
                        x, make_const_int(ty, cy->value_ - 1), bb, true);
                    stampIntegerFacts(andInst);
                    bb->add_instruction_before_inst(andInst, inst);
                    return andInst;
                }
            }
        }
    }
    // (b) No icmp-zero pattern, but still safe if we can prove x ≥ 0
    //     or x is an exact multiple of 2^k.
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        int k = log2Int(cy->value_);
        if (isKnownMultipleOf(x, k, bb))
            return make_const_int(ty, 0);
        if (isKnownNonNegative(x)) {
            auto *andInst = new BinaryInst(ty, Instruction::And,
                x, make_const_int(ty, cy->value_ - 1), bb, true);
            stampIntegerFacts(andInst);
            bb->add_instruction_before_inst(andInst, inst);
            return andInst;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFMul  —  floating-point FMul simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFMul(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    // 1. Constant fold: C1 * C2 → C3
    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    // 2. Canonicalize: constant to RHS  (fmul C, x → fmul x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::FMul, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Identity: x * 1.0 → x
    if (cy && cy->value_ == 1.0f) {
        return x;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFDiv  —  floating-point FDiv simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFDiv(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    // 1. Constant fold: C1 / C2 → C3  (guard division by zero)
    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    // 2. Identity: x / 1.0 → x
    if (cy && cy->value_ == 1.0f) {
        return x;
    }

    return nullptr;
}
