#include "instCombineInternal.hpp"

static bool isPowerOfTwo(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static int log2Int(int v) {
    int r = 0;
    while (v >>= 1) ++r;
    return r;
}

// ═══════════════════════════════════════════════════════════════════════
// visitMul  —  integer Mul simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitMul(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 * C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ * cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (mul C, x → mul x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Mul, y, x, bb, true);
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
                auto *new_inst = new BinaryInst(ty, Instruction::Mul,
                    x_inst->get_operand(0),
                    make_const_int(ty, c1->value_ * cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    if (!cy) return nullptr;

    // 6. Strength reduction: mul x, 2^k  →  shl x, k
    //    1-to-1 replace, universally beneficial regardless of target.
    if (isPowerOfTwo(cy->value_) && cy->value_ > 1) {
        int shift = log2Int(cy->value_);
        auto *shl = new BinaryInst(ty, Instruction::Shl, x,
            make_const_int(ty, shift), bb, true);
        bb->add_instruction_before_inst(shl, inst);
        return shl;
    }

    // Non-power-of-two constant multipliers are left as mul.
    // AArch64 can fuse shift+add/sub for many patterns (e.g.
    // x*3 → add x, x, x, lsl #1).  Decomposing in IR would
    // prevent the backend from recognising those fused forms.

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSDiv  —  integer SDiv simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSDiv(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 / C2 → C3  (guard division by zero)
    if (cx && cy) {
        if (cy->value_ == 0) return nullptr;
        return make_const_int(ty, cx->value_ / cy->value_);
    }

    // 2. Identity: x / 1 → x
    if (cy && cy->value_ == 1) {
        return x;
    }

    // 3. Zero dividend: 0 / x → 0  (x ≠ 0)
    if (cx && cx->value_ == 0) {
        if (cy && cy->value_ == 0) return nullptr;
        return make_const_int(ty, 0);
    }

    // 4. x / -1  →  sub 0, x
    if (cy && cy->value_ == -1) {
        auto *neg = new BinaryInst(ty, Instruction::Sub,
            make_const_int(ty, 0), x, bb, true);
        bb->add_instruction_before_inst(neg, inst);
        return neg;
    }

    // 5. sdiv x, 2^k  →  ashr x, k
    //    Correct when x ≥ 0 
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        int k = log2Int(cy->value_);
        auto *ashr = new BinaryInst(ty, Instruction::AShr, x,
            make_const_int(ty, k), bb, true);
        bb->add_instruction_before_inst(ashr, inst);
        return ashr;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSRem  —  integer SRem simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSRem(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 % C2 → C3  (guard division by zero)
    if (cx && cy) {
        if (cy->value_ == 0) return nullptr;
        return make_const_int(ty, cx->value_ % cy->value_);
    }

    // 2. Identity: x % 1 → 0
    if (cy && cy->value_ == 1) {
        return make_const_int(ty, 0);
    }

    // 3. Zero dividend: 0 % x → 0  (x ≠ 0)
    if (cx && cx->value_ == 0) {
        if (cy && cy->value_ == 0) return nullptr;
        return make_const_int(ty, 0);
    }

    // 4. srem x, 2^k  →  and x, 2^k-1
    //    Replaces an expensive srem (4-20c) with a single and (1c).
    //    Correct when x ≥ 0. 
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        auto *andInst = new BinaryInst(ty, Instruction::And,
            x, make_const_int(ty, cy->value_ - 1), bb, true);
        bb->add_instruction_before_inst(andInst, inst);
        return andInst;
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
        return make_const_float(ty, cx->value_ * cy->value_);
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

    // 4. Zero: x * 0.0 → 0.0  (ignoring NaN * 0.0 edge case)
    if (cy && cy->value_ == 0.0f) {
        return make_const_float(ty, 0.0f);
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
        if (cy->value_ == 0.0f) return nullptr;
        return make_const_float(ty, cx->value_ / cy->value_);
    }

    // 2. Identity: x / 1.0 → x
    if (cy && cy->value_ == 1.0f) {
        return x;
    }

    // 3. Zero dividend: 0.0 / x → 0.0  (ignoring 0.0/0.0 edge case)
    if (cx && cx->value_ == 0.0f) {
        if (cy && cy->value_ == 0.0f) return nullptr;
        return make_const_float(ty, 0.0f);
    }

    return nullptr;
}
