#include "instCombineInternal.hpp"

#include <cmath>

// ═══════════════════════════════════════════════════════════════════════
// visitAdd — integer Add
//
// Capabilities (SysY int / LLVM-aligned, no UB):
//   - Constant fold C1+C2; canonicalize constant to RHS
//   - Identities: x+0 → x; x+x → shl x,1
//   - Reassoc: (x+C1)+C2 → x+(C1+C2)
//   - Neg cancel: x+(0-y) / (0-y)+x → x-y
//   - A53-friendly: x+(x<<k) / (x<<k)+x → mul x, 2^k+1
//   - Disjoint add: add x,C → or x,C when low bits of x proven zero
//   - Side effect: stamp NSW/NUW on add x,1 when dominated by x<bound
// ═══════════════════════════════════════════════════════════════════════

Value* visitAdd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    if (cx && !cy) {
        if (inst->get_operand(1) == x)
            return nullptr;
        auto *new_inst = new BinaryInst(ty, Instruction::Add, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == 0)
        return x;

    if (x == y) {
        auto *shl = new BinaryInst(ty, Instruction::Shl, x,
                                   make_const_int(ty, 1), bb, true);
        stampIntegerFacts(shl);
        bb->add_instruction_before_inst(shl, inst);
        return shl;
    }

    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_add()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                int combined;
                if (!ConstantEvaluator::foldIntegerBinary(
                        Instruction::Add, c1->value_, cy->value_, combined))
                    return nullptr;
                if (x_inst->get_operand(0) == x && combined == c1->value_)
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Add,
                    x_inst->get_operand(0),
                    make_const_int(ty, combined), bb, true);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // x + (0 - y)  /  (0 - y) + x  →  x - y
    {
        auto tryNeg = [&](Value *negCandidate, Value *other) -> Value* {
            auto *neg = dynamic_cast<Instruction*>(negCandidate);
            if (!neg || !neg->is_sub()) return nullptr;
            auto *zero = as_const_int(neg->get_operand(0));
            if (!zero || zero->value_ != 0) return nullptr;
            auto *sub = new BinaryInst(ty, Instruction::Sub, other,
                                       neg->get_operand(1), bb, true);
            stampIntegerFacts(sub);
            bb->add_instruction_before_inst(sub, inst);
            return sub;
        };
        if (auto *r = tryNeg(y, x)) return r;
        if (auto *r = tryNeg(x, y)) return r;
    }

    {
        auto *xi = dynamic_cast<Instruction*>(x);
        auto *yi = dynamic_cast<Instruction*>(y);
        if (xi && xi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(xi->get_operand(1));
            if (amt && amt->value_ >= 0 && amt->value_ < 31 &&
                xi->get_operand(0) == y) {
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    y, make_const_int(ty, (1 << amt->value_) + 1), bb, true);
                stampIntegerFacts(mul);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
        if (yi && yi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(yi->get_operand(1));
            if (amt && amt->value_ >= 0 && amt->value_ < 31 &&
                yi->get_operand(0) == x) {
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    x, make_const_int(ty, (1 << amt->value_) + 1), bb, true);
                stampIntegerFacts(mul);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
    }

    if (cy && cy->value_ > 0) {
        int k = 1;
        while (k < 31 && (1 << k) <= cy->value_) k++;
        if (isKnownMultipleOf(x, k, bb)) {
            auto *or_inst = new BinaryInst(ty, Instruction::Or, x, y, bb, true);
            stampIntegerFacts(or_inst);
            bb->add_instruction_before_inst(or_inst, inst);
            return or_inst;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSub — integer Sub
//
// Capabilities:
//   - Constant fold C1-C2; identities x-0 → x, x-x → 0
//   - Reassoc: (x+C1)-C2 → x+(C1-C2); (x-C1)-C2 → x-(C1+C2)
//   - Neg cancel: x-(0-y) → x+y
//   - Algebra: x-(x+y) → 0-y; x-(x-y) → y; (x+y)-x → y; (x+y)-y → x
//   - A53-friendly: (x<<k)-x → mul x, 2^k-1
// ═══════════════════════════════════════════════════════════════════════

Value* visitSub(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    if (cy && cy->value_ == 0)
        return x;

    if (x == y)
        return make_const_int(ty, 0);

    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_add()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                int combined;
                if (!ConstantEvaluator::foldIntegerBinary(
                        Instruction::Sub, c1->value_, cy->value_, combined))
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Add,
                    x_inst->get_operand(0),
                    make_const_int(ty, combined), bb, true);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_sub()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                int combined;
                if (!ConstantEvaluator::foldIntegerBinary(
                        Instruction::Add, c1->value_, cy->value_, combined))
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Sub,
                    x_inst->get_operand(0),
                    make_const_int(ty, combined), bb, true);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    // x - (0 - y) → x + y
    {
        auto *yi = dynamic_cast<Instruction*>(y);
        if (yi && yi->is_sub()) {
            auto *zero = as_const_int(yi->get_operand(0));
            if (zero && zero->value_ == 0) {
                auto *add = new BinaryInst(ty, Instruction::Add, x,
                                           yi->get_operand(1), bb, true);
                stampIntegerFacts(add);
                bb->add_instruction_before_inst(add, inst);
                return add;
            }
        }
    }

    // x - (x + y) → 0 - y ;  x - (y + x) → 0 - y
    // x - (x - y) → y
    // (x + y) - x → y ;  (y + x) - x → y
    {
        auto *yi = dynamic_cast<Instruction*>(y);
        if (yi && yi->is_add()) {
            Value *a = yi->get_operand(0), *b = yi->get_operand(1);
            if (a == x || b == x) {
                Value *other = (a == x) ? b : a;
                auto *neg = new BinaryInst(ty, Instruction::Sub,
                    make_const_int(ty, 0), other, bb, true);
                stampIntegerFacts(neg);
                bb->add_instruction_before_inst(neg, inst);
                return neg;
            }
        }
        if (yi && yi->is_sub() && yi->get_operand(0) == x)
            return yi->get_operand(1);

        auto *xi = dynamic_cast<Instruction*>(x);
        if (xi && xi->is_add()) {
            Value *a = xi->get_operand(0), *b = xi->get_operand(1);
            if (a == y) return b;
            if (b == y) return a;
        }
    }

    {
        auto *xi = dynamic_cast<Instruction*>(x);
        if (xi && xi->op_id_ == Instruction::Shl) {
            auto *amt = as_const_int(xi->get_operand(1));
            if (amt && amt->value_ >= 0 && amt->value_ < 31 &&
                xi->get_operand(0) == y) {
                auto *mul = new BinaryInst(ty, Instruction::Mul,
                    y, make_const_int(ty, (1 << amt->value_) - 1), bb, true);
                stampIntegerFacts(mul);
                bb->add_instruction_before_inst(mul, inst);
                return mul;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFAdd — floating-point FAdd
//
// Capabilities (finite SysY float; signed-zero–safe where noted):
//   - Constant fold; canonicalize constant to RHS
//   - Identity: x + (-0.0) → x  (IEEE-safe; +0.0 is not folded)
//   - fneg(x) + fneg(y) → fneg(x + y)
// ═══════════════════════════════════════════════════════════════════════

Value* visitFAdd(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::FAdd, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // fadd x, -0.0 → x  (adding -0 never changes a finite/NaN value)
    if (cy && cy->value_ == 0.0f && std::signbit(cy->value_))
        return x;

    {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        auto *y_inst = dynamic_cast<Instruction*>(y);
        if (x_inst && x_inst->op_id_ == Instruction::FNeg &&
            y_inst && y_inst->op_id_ == Instruction::FNeg) {
            Value *inner_x = x_inst->get_operand(0);
            Value *inner_y = y_inst->get_operand(0);
            auto *inner_add = new BinaryInst(ty, Instruction::FAdd,
                                             inner_x, inner_y, bb, true);
            bb->add_instruction_before_inst(inner_add, inst);
            auto *fneg = new UnaryInst(ty, Instruction::FNeg, inner_add, bb, true);
            bb->add_instruction_before_inst(fneg, inst);
            return fneg;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFSub — floating-point FSub
//
// Capabilities:
//   - Constant fold
//   - Identity: x - (+0.0) → x
//   - Neg form: (-0.0) - x → fneg x
//   - fsub x, fneg(y) → fadd x, y
// ═══════════════════════════════════════════════════════════════════════

Value* visitFSub(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    if (cy && cy->value_ == 0.0f && !std::signbit(cy->value_))
        return x;

    if (cx && cx->value_ == 0.0f && std::signbit(cx->value_)) {
        auto *fneg = new UnaryInst(ty, Instruction::FNeg, y, bb, true);
        bb->add_instruction_before_inst(fneg, inst);
        return fneg;
    }

    {
        auto *y_inst = dynamic_cast<Instruction*>(y);
        if (y_inst && y_inst->op_id_ == Instruction::FNeg) {
            Value *inner_y = y_inst->get_operand(0);
            auto *new_inst = new BinaryInst(ty, Instruction::FAdd,
                                            x, inner_y, bb, true);
            bb->add_instruction_before_inst(new_inst, inst);
            return new_inst;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFNeg — floating-point FNeg
//
// Capabilities:
//   - Constant fold fneg C → -C
//   - Double negate: fneg(fneg(x)) → x
//   - fneg(fsub(x, y)) → fsub(y, x)
// ═══════════════════════════════════════════════════════════════════════

Value* visitFNeg(UnaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    auto *cx = as_const_float(x);
    if (cx)
        return make_const_float(ty, -cx->value_);

    auto *x_inst = dynamic_cast<Instruction*>(x);

    if (x_inst && x_inst->op_id_ == Instruction::FNeg)
        return x_inst->get_operand(0);

    if (x_inst && x_inst->is_fsub()) {
        Value *inner_x = x_inst->get_operand(0);
        Value *inner_y = x_inst->get_operand(1);
        auto *new_inst = new BinaryInst(ty, Instruction::FSub,
                                        inner_y, inner_x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    return nullptr;
}
