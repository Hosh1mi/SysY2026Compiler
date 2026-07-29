#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitAnd — integer bitwise AND
//
// Capabilities:
//   - Constant fold; canonicalize constant to RHS
//   - Identities: x&(-1) → x, x&0 → 0, x&x → x
//   - Known-zero bits: if all selected bits of x are zero → 0
//   - Reassoc: (x&C1)&C2 → x&(C1&C2)
//   - Absorption: x&(x|y) / (x|y)&x → x
//   - Parity: and(mul(a,b),1) → and(and(a,1),and(b,1)) when mul single-use
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

    if (cx && cy)
        return make_const_int(ty, cx->value_ & cy->value_);

    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::And, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == -1)
        return x;

    if (cy && cy->value_ == 0)
        return make_const_int(ty, 0);

    if (cy && ValueFacts::hasKnownZeroBits(
                  x, static_cast<uint32_t>(cy->value_)))
        return make_const_int(ty, 0);

    if (x == y)
        return x;

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

    // Absorption: x & (x | y) → x
    {
        auto absorbs = [](Value *common, Value *orVal) -> bool {
            auto *oi = dynamic_cast<Instruction*>(orVal);
            if (!oi || oi->op_id_ != Instruction::Or) return false;
            return oi->get_operand(0) == common || oi->get_operand(1) == common;
        };
        if (absorbs(x, y)) return x;
        if (absorbs(y, x)) return y;
    }

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
// visitOr — integer bitwise OR
//
// Capabilities:
//   - Constant fold; canonicalize constant to RHS
//   - Identities: x|0 → x, x|(-1) → -1, x|x → x
//   - Reassoc: (x|C1)|C2 → x|(C1|C2)
//   - Absorption: x|(x&y) / (x&y)|x → x
//   - Side effect: stamp Disjoint when operands have no overlapping bits
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

    if (cx && cy)
        return make_const_int(ty, cx->value_ | cy->value_);

    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Or, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == 0)
        return x;

    if (cy && cy->value_ == -1)
        return make_const_int(ty, -1);

    if (x == y)
        return x;

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

    // Absorption: x | (x & y) → x
    {
        auto absorbs = [](Value *common, Value *andVal) -> bool {
            auto *ai = dynamic_cast<Instruction*>(andVal);
            if (!ai || ai->op_id_ != Instruction::And) return false;
            return ai->get_operand(0) == common || ai->get_operand(1) == common;
        };
        if (absorbs(x, y)) return x;
        if (absorbs(y, x)) return y;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitXor — integer bitwise XOR
//
// Capabilities:
//   - Constant fold; canonicalize constant to RHS
//   - Identities: x^0 → x, x^x → 0
//   - Reassoc: (x^C1)^C2 → x^(C1^C2)  (covers double not: x^(-1)^(-1) → x)
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

    if (cx && cy)
        return make_const_int(ty, cx->value_ ^ cy->value_);

    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::Xor, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == 0)
        return x;

    if (x == y)
        return make_const_int(ty, 0);

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
