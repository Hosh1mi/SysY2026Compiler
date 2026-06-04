#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// Helper: swap predicate when moving a constant from LHS to RHS
// ═══════════════════════════════════════════════════════════════════════

static ICmpInst::ICmpOp getSwappedPredicate(ICmpInst::ICmpOp op) {
    switch (op) {
        case ICmpInst::ICMP_EQ:  return ICmpInst::ICMP_EQ;   // symmetric
        case ICmpInst::ICMP_NE:  return ICmpInst::ICMP_NE;   // symmetric
        case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLT;
        case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLE;
        case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGT;
        case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGE;
        case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULT;
        case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULE;
        case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGT;
        case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGE;
        default:
            assert(0 && "Unknown ICmp predicate");
            return ICmpInst::ICMP_EQ;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// visitICmp  —  integer comparison simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitICmp(ICmpInst *inst) {
    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;  // i1
    BasicBlock *bb = inst->parent_;
    ICmpInst::ICmpOp pred = inst->icmp_op_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    // 1. Constant fold: evaluate at compile time
    if (cx && cy) {
        bool result = false;
        switch (pred) {
            case ICmpInst::ICMP_EQ:  result = (cx->value_ == cy->value_); break;
            case ICmpInst::ICMP_NE:  result = (cx->value_ != cy->value_); break;
            case ICmpInst::ICMP_SGT: result = (cx->value_ >  cy->value_); break;
            case ICmpInst::ICMP_SGE: result = (cx->value_ >= cy->value_); break;
            case ICmpInst::ICMP_SLT: result = (cx->value_ <  cy->value_); break;
            case ICmpInst::ICMP_SLE: result = (cx->value_ <= cy->value_); break;
            case ICmpInst::ICMP_UGT: result = ((unsigned)cx->value_ >  (unsigned)cy->value_); break;
            case ICmpInst::ICMP_UGE: result = ((unsigned)cx->value_ >= (unsigned)cy->value_); break;
            case ICmpInst::ICMP_ULT: result = ((unsigned)cx->value_ <  (unsigned)cy->value_); break;
            case ICmpInst::ICMP_ULE: result = ((unsigned)cx->value_ <= (unsigned)cy->value_); break;
            default: return nullptr;
        }
        return make_const_int(ty, result ? 1 : 0);
    }

    // 2. Canonicalize: constant to RHS, swap predicate
    if (cx && !cy) {
        ICmpInst::ICmpOp swapped = getSwappedPredicate(pred);
        auto *new_inst = new ICmpInst(swapped, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    // 3. Self-comparisons
    if (x == y) {
        switch (pred) {
            case ICmpInst::ICMP_EQ:
            case ICmpInst::ICMP_SGE:
            case ICmpInst::ICMP_SLE:
            case ICmpInst::ICMP_UGE:
            case ICmpInst::ICMP_ULE:
                return make_const_int(ty, 1);  // true
            case ICmpInst::ICMP_NE:
            case ICmpInst::ICMP_SGT:
            case ICmpInst::ICMP_SLT:
            case ICmpInst::ICMP_UGT:
            case ICmpInst::ICMP_ULT:
                return make_const_int(ty, 0);  // false
            default:
                return nullptr;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSelect  —  select instruction simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitSelect(SelectInst *inst) {
    Value *cond  = inst->get_operand(0);
    Value *tval  = inst->get_operand(1);
    Value *fval  = inst->get_operand(2);
    Type  *ty    = inst->type_;
    BasicBlock *bb = inst->parent_;

    // 1. Constant condition: select true, x, y → x
    auto *cc = as_const_int(cond);
    if (cc) {
        if (cc->value_ != 0) return tval;  // true
        return fval;                        // false
    }

    // 2. Same value on both arms: select c, x, x → x
    if (tval == fval) {
        return tval;
    }

    // 3. Boolean canonicalization: select c, 1, 0 → zext c
    //    Only for i32 result (the common case in SysY).
    if (ty->tid_ == Type::IntegerTyID) {
        auto *ct = as_const_int(tval);
        auto *cf = as_const_int(fval);
        if (ct && cf && ct->value_ == 1 && cf->value_ == 0) {
            auto *zext = new ZextInst(Instruction::ZExt, cond, ty, bb, true);
            bb->add_instruction_before_inst(zext, inst);
            return zext;
        }
    }

    return nullptr;
}
