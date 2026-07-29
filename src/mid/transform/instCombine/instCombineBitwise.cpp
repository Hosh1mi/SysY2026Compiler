#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitAnd  —  integer bitwise AND simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitAnd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

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
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
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

    // Every selected bit is already known zero.
    if (cy && ValueFacts::hasKnownZeroBits(
                  x, static_cast<uint32_t>(cy->value_))) {
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
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // 7. Parity extraction: and(mul(a, b), 1)  →  and(and(a, 1), and(b, 1))
    //    Proof: (a*b) mod 2 = (a mod 2)*(b mod 2) = (a&1)&(b&1) for all i32.
    //    The mask MUST be exactly 1; the identity does NOT hold for 2^k (k>0):
    //    e.g. (3*3)&4 = 4 but (3&4)&(3&4) = 0.
    //    Require the mul to have a single user so DCE can delete it afterward;
    //    if the product is used elsewhere, adding 3 ands for no net gain is wrong.
    if (cy && cy->value_ == 1) {
        auto *mul_inst = dynamic_cast<BinaryInst*>(x);
        if (mul_inst && mul_inst->op_id_ == Instruction::Mul &&
            mul_inst->use_list_.size() == 1) {
            Value *a = mul_inst->get_operand(0);
            Value *b = mul_inst->get_operand(1);
            auto *a1 = new BinaryInst(ty, Instruction::And, a,
                                      make_const_int(ty, 1), bb, true);
            stampIntegerFacts(a1);
            bb->add_instruction_before_inst(a1, inst);
            auto *b1 = new BinaryInst(ty, Instruction::And, b,
                                      make_const_int(ty, 1), bb, true);
            stampIntegerFacts(b1);
            bb->add_instruction_before_inst(b1, inst);
            auto *result = new BinaryInst(ty, Instruction::And, a1, b1,
                                          bb, true);
            stampIntegerFacts(result);
            bb->add_instruction_before_inst(result, inst);
            return result;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitOr  —  integer bitwise OR simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitOr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

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
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
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
                stampIntegerFacts(new_inst);
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
    stampIntegerFacts(inst);

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
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
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
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}
