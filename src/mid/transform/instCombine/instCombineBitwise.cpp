#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitAnd  —  integer bitwise AND simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitAnd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    // 1. Constant fold: C1 & C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ & cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (and C, x → and x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::And, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Identity: x & -1 (all ones) → x
    if (cy && cy->value_ == -1) {
        return x;
    }

    // 4. Zero annihilation: x & 0 → 0
    if (cy && cy->value_ == 0) {
        return make_const_int(ty, 0);
    }

    // 5. Idempotence: x & x → x
    if (x == y) {
        return x;
    }

    // 6. Reassociation: (x & C1) & C2 → x & (C1 & C2)
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::And) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::And,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ & cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitOr  —  integer bitwise OR simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitOr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 | C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ | cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (or C, x → or x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Or, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Identity: x | 0 → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. All-ones annihilation: x | -1 → -1
    if (cy && cy->value_ == -1) {
        return make_const_int(ty, -1);
    }

    // 5. Idempotence: x | x → x
    if (x == y) {
        return x;
    }

    // 6. Reassociation: (x | C1) | C2 → x | (C1 | C2)
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::Or) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::Or,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ | cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitXor  —  integer bitwise XOR simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitXor(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: C1 ^ C2 → C3
    if (cx && cy) {
        return make_const_int(ty, cx->value_ ^ cy->value_);
    }

    // 2. Canonicalize: constant to RHS  (xor C, x → xor x, C)
    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Xor, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Identity: x ^ 0 → x
    if (cy && cy->value_ == 0) {
        return x;
    }

    // 4. Self-cancel: x ^ x → 0
    if (x == y) {
        return make_const_int(ty, 0);
    }

    // 5. Reassociation: (x ^ C1) ^ C2 → x ^ (C1 ^ C2)
    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::Xor) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1) {
                auto *new_inst = new BinaryInst(ty, Instruction::Xor,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ ^ cy->value_), bb, true);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}
