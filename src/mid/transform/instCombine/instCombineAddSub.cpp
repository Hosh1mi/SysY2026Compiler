// NOTE: If bugs are met, delete fneg
#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitAdd  —  integer Add simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitAdd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 + C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ + cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (add C, x → add x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Add, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // After canonicalization, any constant (if present) is on the RHS.

    // 3. Identity: x + 0 → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. Reassociation: (add x, C1) + C2 → add x, C1+C2
    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_add()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::Add,
                    x_inst->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // 5. Fold  x + (x << k)  →  mul x, 2^k+1
    //    Canonical form for the backend: a single mul lets the backend
    //    emit one fused  add x, x, lsl #k  on AArch64.
    {
        auto *xi = dynamic_cast<Instruction*>(x);
        auto *yi = dynamic_cast<Instruction*>(y);
        if (xi && xi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(xi->get_operand(1));
            if (amt && xi->get_operand(0) == y) {
                // (x << k) + x
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    y, make_const_int(ty, (1 << amt->value_) + 1), bb, true);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
        if (yi && yi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(yi->get_operand(1));
            if (amt && yi->get_operand(0) == x) {
                // x + (x << k)
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    x, make_const_int(ty, (1 << amt->value_) + 1), bb, true);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
    }

    // 6. add x, C  →  or x, C   when  x & C == 0  (no carry possible)
    //    If x has k trailing zeros and C fits in k bits, addition is just bitwise OR.
    if (cy && cy->value_ > 0) {
        // k = ⌈log₂(C+1)⌉ — the number of bits needed to represent C
        int k = 1;
        while ((1 << k) <= cy->value_) k++;
        // C < 2^k, so all set bits of C are in positions 0..k-1.
        // If x is a multiple of 2^k, its low k bits are zero → x & C == 0.
        if (isKnownMultipleOf(x, k, bb)) {
            auto *or_inst = new BinaryInst(ty, Instruction::Or, x, y, bb, true);
            bb->add_instruction_before_inst(or_inst, inst);
            return or_inst;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSub  —  integer Sub simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSub(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 - C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ - cy->value_);
    }

    // 2. Identity: x - 0 → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 3. Self-cancel: x - x → 0
    if (x == y) {
        return make_const_int(ty, 0);
    }

    // 4. Reassociation: (add x, C1) - C2 → add x, C1-C2
    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_add()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::Add,
                    x_inst->get_operand(0),
                    make_const_int(ty, c1->value_ - cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // 5. Reassociation: (sub x, C1) - C2 → sub x, C1+C2
    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_sub()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::Sub,
                    x_inst->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // 6. Fold  (x << k) - x  →  mul x, 2^k-1
    {
        auto *xi = dynamic_cast<Instruction*>(x);
        if (xi && xi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(xi->get_operand(1));
            if (amt && xi->get_operand(0) == y) {
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    y, make_const_int(ty, (1 << amt->value_) - 1), bb, true);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFAdd  —  floating-point FAdd simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFAdd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    // 1. Constant fold: C1 + C2 → C3
    if (cx && cy) {
        return make_const_float(ty, cx->value_ + cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (fadd C, x → fadd x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::FAdd, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Identity: x + 0.0 → x   (ignoring signed-zero IEEE edge cases)
    if (cy && cy->value_ == 0.0f) {
        return x;
    }

    // 4. fneg(x) + fneg(y) → fneg(x + y)
    {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        auto *y_inst = dynamic_cast<Instruction*>(y);
        if (x_inst && x_inst->op_id_ == Instruction::FNeg &&
            y_inst && y_inst->op_id_ == Instruction::FNeg) {
            Value *inner_x = x_inst->get_operand(0);
            Value *inner_y = y_inst->get_operand(0);
            // inner add
            auto *inner_add = new BinaryInst(ty, Instruction::FAdd, inner_x, inner_y, bb, true);
            bb->add_instruction_before_inst(inner_add, inst);
            // outer fneg
            auto *fneg = new UnaryInst(ty, Instruction::FNeg, inner_add, bb, true);
            bb->add_instruction_before_inst(fneg, inst);
            return fneg;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFSub  —  floating-point FSub simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFSub(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    // 1. Constant fold: C1 - C2 → C3
    if (cx && cy) {
        return make_const_float(ty, cx->value_ - cy->value_);
    }

    // 2. Identity: x - 0.0 → x
    if (cy && cy->value_ == 0.0f) {
        return x;
    }

    // 3. Self-cancel: x - x → 0.0
    if (x == y) {
        return make_const_float(ty, 0.0f);
    }

    // 4. fsub x, fneg(y) → fadd x, y
    {
        auto *y_inst = dynamic_cast<Instruction*>(y);
        if (y_inst && y_inst->op_id_ == Instruction::FNeg) {
            Value *inner_y = y_inst->get_operand(0);
            auto *new_inst = new BinaryInst(ty, Instruction::FAdd, x, inner_y, bb, true);
            bb->add_instruction_before_inst(new_inst, inst);
            return new_inst;
        }
    }

    // 5. fsub -0.0, x → fneg x   (also handles +0.0; mathematically sound)
    if (cx && cx->value_ == 0.0f) {
        auto *fneg = new UnaryInst(ty, Instruction::FNeg, y, bb, true);
        bb->add_instruction_before_inst(fneg, inst);
        return fneg;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFNeg  —  floating-point FNeg simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFNeg(UnaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    // 1. Constant fold: fneg C → -C
    auto *cx = as_const_float(x);
    if (cx) {
        return make_const_float(ty, -cx->value_);
    }

    auto *x_inst = dynamic_cast<Instruction*>(x);

    // 2. Double negate: fneg(fneg(x)) → x
    if (x_inst && x_inst->op_id_ == Instruction::FNeg) {
        return x_inst->get_operand(0);
    }

    // 3. fneg(fsub(x, y)) → fsub(y, x)
    if (x_inst && x_inst->is_fsub()) {
        Value *inner_x = x_inst->get_operand(0);
        Value *inner_y = x_inst->get_operand(1);
        auto *new_inst = new BinaryInst(ty, Instruction::FSub, inner_y, inner_x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    return nullptr;
}
