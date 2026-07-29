#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitShl — integer Shift-Left
//
// Capabilities:
//   - Constant fold (overshift → 0); identity x<<0 → x; overshift → 0
//   - Merge: (x<<C1)<<C2 → x<<(C1+C2) when sum < bitwidth
//   - Non-commutative: never swap operands (shl C,x ≠ shl x,C)
// ═══════════════════════════════════════════════════════════════════════

Value* visitShl(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    if (cx && cy) {
        if (cy->value_ < 0) return nullptr;
        if (cy->value_ < (int)bits)
            return make_const_int(ty, static_cast<int32_t>(
                static_cast<uint32_t>(cx->value_) << cy->value_));
        return make_const_int(ty, 0);
    }

    if (cy && cy->value_ == 0)
        return x;

    if (cy && cy->value_ >= (int)bits)
        return make_const_int(ty, 0);

    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::Shl) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                if (inner->get_operand(0) == x && c1->value_ == 0)
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Shl,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                copySemFlags(inst, new_inst);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitLShr — integer Logical-Shift-Right
//
// Capabilities:
//   - Constant fold; identity; overshift → 0
//   - Merge: (x>>>C1)>>>C2 → x>>>(C1+C2) when sum < bitwidth
//   - Mask: (x<<C)>>>C → and x, (2^(bits-C)-1)  (high bits cleared)
// ═══════════════════════════════════════════════════════════════════════

Value* visitLShr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    if (cx && cy) {
        if (cy->value_ < 0) return nullptr;
        if (cy->value_ < (int)bits) {
            unsigned uval = (unsigned)(cx->value_);
            return make_const_int(ty, (int)(uval >> cy->value_));
        }
        return make_const_int(ty, 0);
    }

    if (cy && cy->value_ == 0)
        return x;

    if (cy && cy->value_ >= (int)bits)
        return make_const_int(ty, 0);

    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::LShr) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                auto *new_inst = new BinaryInst(ty, Instruction::LShr,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                copySemFlags(inst, new_inst);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }

        // (x << C) >>> C → x & ((1 << (bits-C)) - 1)
        if (inner && inner->op_id_ == Instruction::Shl) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ == cy->value_ &&
                cy->value_ > 0 && cy->value_ < (int)bits) {
                // C in [1, bits): bits-C in [1, bits-1], so 1u<<(bits-C) is defined.
                uint32_t mask = (1u << (bits - cy->value_)) - 1u;
                auto *andInst = new BinaryInst(ty, Instruction::And,
                    inner->get_operand(0),
                    make_const_int(ty, static_cast<int>(mask)), bb, true);
                stampIntegerFacts(andInst);
                bb->add_instruction_before_inst(andInst, inst);
                return andInst;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitAShr — integer Arithmetic-Shift-Right
//
// Capabilities:
//   - Constant fold; identity; overshift → ashr x, bits-1
//   - Merge: (x>>C1)>>C2 → x>>(C1+C2) when sum < bitwidth
//   - Side effect: stamp Exact when shifted value is multiple of 2^amt
// ═══════════════════════════════════════════════════════════════════════

Value* visitAShr(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);
    unsigned bits = static_cast<IntegerType*>(ty)->num_bits_;

    if (cx && cy) {
        if (cy->value_ < 0) return nullptr;
        if (cy->value_ < (int)bits)
            return make_const_int(ty, cx->value_ >> cy->value_);
        return make_const_int(ty, cx->value_ >> (bits - 1));
    }

    if (cy && cy->value_ == 0)
        return x;

    if (cy && cy->value_ >= (int)bits) {
        auto *new_inst = new BinaryInst(ty, Instruction::AShr,
            x, make_const_int(ty, bits - 1), bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy) {
        auto *inner = dynamic_cast<Instruction*>(x);
        if (inner && inner->op_id_ == Instruction::AShr) {
            auto *c1 = as_const_int(inner->get_operand(1));
            if (c1 && c1->value_ + cy->value_ < (int)bits) {
                auto *new_inst = new BinaryInst(ty, Instruction::AShr,
                    inner->get_operand(0),
                    make_const_int(ty, c1->value_ + cy->value_), bb, true);
                copySemFlags(inst, new_inst);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    return nullptr;
}
