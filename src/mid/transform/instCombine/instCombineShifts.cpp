#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitShl  —  integer Shift-Left simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitShl(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    // 1. Constant fold: C1 << C2 → C3
    if (cx && cy) {
        if (cy->value_ < (int)bits)
            return make_const_int(ty, cx->value_ << cy->value_);
        return make_const_int(ty, 0);
    }

    // Shift is non-commutative, so we cannot canonicalize a constant LHS to
    // the RHS (the old `shl C, x → shl x, C` rule was a copy-paste from a
    // commutative-op simplification and silently corrupted results when the
    // shift amount was symbolic, e.g. the `1 << n` produced by VAR_SHL
    // rewriting of rotlN).

    // 3. Identity: x << 0 → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. Overshift: x << C  where C >= bitwidth  →  0
    if (cy && cy->value_ >= (int)bits) {
        return make_const_int(ty, 0);
    }

    // 5. Redundant shift: (x << C1) << C2  →  x << (C1 + C2)
    //    Safe when C1 + C2 < bitwidth.
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::Shl) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                auto *new_inst = new BinaryInst(ty, Instruction::Shl,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitLShr  —  integer Logical-Shift-Right simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitLShr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    // 1. Constant fold: C1 >>> C2 → C3
    if (cx && cy) {
        if (cy->value_ < (int)bits) {
            // Logical shift: treat value as unsigned
            unsigned uval = (unsigned)(cx->value_);
            return make_const_int(ty, (int)(uval >> cy->value_));
        }
        return make_const_int(ty, 0);
    }

    // (Shift is non-commutative — see visitShl note.)

    // 3. Identity: x >> 0 (logical) → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. Overshift: x >> C (logical) where C >= bitwidth → 0
    if (cy && cy->value_ >= (int)bits) {
        return make_const_int(ty, 0);
    }

    // 5. Redundant shift: (x >>> C1) >>> C2  →  x >>> (C1 + C2)
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::LShr) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                auto *new_inst = new BinaryInst(ty, Instruction::LShr,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitAShr  —  integer Arithmetic-Shift-Right simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitAShr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    // 1. Constant fold: C1 >> C2 (arithmetic) → C3
    if (cx && cy) {
        if (cy->value_ < (int)bits)
            return make_const_int(ty, cx->value_ >> cy->value_);
        // Overshift: fill with sign bit
        return make_const_int(ty, cx->value_ >> (bits - 1));
    }

    // (Shift is non-commutative — see visitShl note.)

    // 3. Identity: x >> 0 (arithmetic) → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. Overshift: x >> C (arithmetic) where C >= bitwidth
    //    → x >> (bits-1)  (sign bit fills all bits)
    if (cy && cy->value_ >= (int)bits) {
        auto *new_inst = new BinaryInst(ty, Instruction::AShr,
            x, make_const_int(ty, bits - 1), bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 5. Redundant shift: (x >> C1) >> C2  →  x >> (C1 + C2)
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::AShr) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                auto *new_inst = new BinaryInst(ty, Instruction::AShr,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}
