#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// Helper: swap predicate when moving a constant from LHS to RHS
// ═══════════════════════════════════════════════════════════════════════

ICmpInst::ICmpOp getSwappedPredicate(ICmpInst::ICmpOp op) {
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

namespace {

struct ScaledValue {
    Value *base = nullptr;
    long long scale = 1;
};

bool parsePositiveScale(Value *v, ScaledValue &out) {
    if (!v) return false;

    if (auto *bin = dynamic_cast<BinaryInst *>(v)) {
        if (bin->op_id_ == Instruction::Mul) {
            if (auto *c = as_const_int(bin->get_operand(1)); c && c->value_ > 0) {
                out = {bin->get_operand(0), c->value_};
                return true;
            }
            if (auto *c = as_const_int(bin->get_operand(0)); c && c->value_ > 0) {
                out = {bin->get_operand(1), c->value_};
                return true;
            }
        }
        if (bin->op_id_ == Instruction::Shl) {
            auto *c = as_const_int(bin->get_operand(1));
            if (c && c->value_ >= 0 && c->value_ < 31) {
                out = {bin->get_operand(0), 1LL << c->value_};
                return true;
            }
        }
    }

    out = {v, 1};
    return true;
}

bool isSourceNonNegative(Value *v, BasicBlock *ctx,
                         std::vector<Value *> &assuming, int depth) {
    if (!v || depth > 12) return false;
    if (std::find(assuming.begin(), assuming.end(), v) != assuming.end())
        return true;

    if (ValueFacts::isKnownNonNegative(v, ctx))
        return true;
    if (gInstCombineRangeAnalysis && gInstCombineRangeAnalysis->isKnownNonNegative(v, ctx))
        return true;

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst) return false;

    switch (inst->op_id_) {
    case Instruction::AShr:
        return isSourceNonNegative(inst->get_operand(0), ctx, assuming, depth + 1);
    case Instruction::Mul: {
        auto *rhs = as_const_int(inst->get_operand(1));
        auto *lhs = as_const_int(inst->get_operand(0));
        if (rhs && rhs->value_ >= 0)
            return isSourceNonNegative(inst->get_operand(0), ctx, assuming, depth + 1);
        if (lhs && lhs->value_ >= 0)
            return isSourceNonNegative(inst->get_operand(1), ctx, assuming, depth + 1);
        return false;
    }
    case Instruction::Shl: {
        auto *shift = as_const_int(inst->get_operand(1));
        return shift && shift->value_ >= 0 && shift->value_ < 31 &&
               isSourceNonNegative(inst->get_operand(0), ctx, assuming, depth + 1);
    }
    case Instruction::Add:
        return isSourceNonNegative(inst->get_operand(0), ctx, assuming, depth + 1) &&
               isSourceNonNegative(inst->get_operand(1), ctx, assuming, depth + 1);
    case Instruction::Or: {
        auto *rhs = as_const_int(inst->get_operand(1));
        auto *lhs = as_const_int(inst->get_operand(0));
        if (rhs && rhs->value_ >= 0)
            return isSourceNonNegative(inst->get_operand(0), ctx, assuming, depth + 1);
        if (lhs && lhs->value_ >= 0)
            return isSourceNonNegative(inst->get_operand(1), ctx, assuming, depth + 1);
        return false;
    }
    default:
        break;
    }

    auto *phi = dynamic_cast<PhiInst *>(inst);
    if (!phi) return false;

    assuming.push_back(v);
    bool ok = true;
    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!isSourceNonNegative(phi->get_operand(i), predBB, assuming, depth + 1)) {
            ok = false;
            break;
        }
    }
    assuming.pop_back();
    return ok;
}

bool isSourceNonNegative(Value *v, BasicBlock *ctx) {
    std::vector<Value *> assuming;
    return isSourceNonNegative(v, ctx, assuming, 0);
}

Value *foldScaledCompareFromPred(ICmpInst *inst) {
    if (!inst || inst->icmp_op_ != ICmpInst::ICMP_SLT || !inst->parent_)
        return nullptr;

    ScaledValue cur;
    if (!parsePositiveScale(inst->get_operand(0), cur))
        return nullptr;
    if (!isSourceNonNegative(cur.base, inst->parent_))
        return nullptr;

    BasicBlock *bb = inst->parent_;
    for (auto *predBB : bb->pre_bbs_) {
        auto *br = dynamic_cast<BranchInst *>(predBB->get_terminator());
        if (!br || br->num_ops_ != 3) continue;

        auto *prevCmp = dynamic_cast<ICmpInst *>(br->get_operand(0));
        if (!prevCmp || prevCmp->icmp_op_ != ICmpInst::ICMP_SLT) continue;
        if (prevCmp->get_operand(1) != inst->get_operand(1)) continue;

        ScaledValue prev;
        if (!parsePositiveScale(prevCmp->get_operand(0), prev)) continue;
        if (prev.base != cur.base) continue;

        auto *trueSucc = dynamic_cast<BasicBlock *>(br->get_operand(1));
        auto *falseSucc = dynamic_cast<BasicBlock *>(br->get_operand(2));

        // Source-level signed arithmetic has undefined overflow. Under that
        // precondition, if k1*x >= bound and x >= 0, then k2*x >= bound for
        // any k2 >= k1.  Keep only the false-edge direction needed by h-1;
        // the true-edge form is too easy to misapply in loop exit tests.
        (void)trueSucc;
        if (falseSucc == bb && cur.scale >= prev.scale)
            return make_const_int(inst->type_, 0);
    }

    return nullptr;
}

} // namespace

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

    if (auto *folded = foldScaledCompareFromPred(inst))
        return folded;

    // // 4. icmp eq/ne (srem x, 2), 1  ->  (x > 0) && ((x & 1) == 1)
    // if (cy && cy->value_ == 1 &&
    //     (pred == ICmpInst::ICMP_EQ || pred == ICmpInst::ICMP_NE)) {
    //     auto *srem = dynamic_cast<BinaryInst *>(x);
    //     auto *divisor = srem ? as_const_int(srem->get_operand(1)) : nullptr;
    //     if (srem && srem->op_id_ == Instruction::SRem && divisor &&
    //         divisor->value_ == 2) {
    //         Value *src = srem->get_operand(0);
    //         auto *mask = new BinaryInst(src->type_, Instruction::And, src,
    //                                     make_const_int(src->type_, 1), bb, true);
    //         stampIntegerFacts(mask);
    //         bb->add_instruction_before_inst(mask, inst);

    //         auto *odd = new ICmpInst(ICmpInst::ICMP_EQ, mask,
    //                                  make_const_int(src->type_, 1), bb, true);
    //         bb->add_instruction_before_inst(odd, inst);

    //         auto *positive = new ICmpInst(ICmpInst::ICMP_SGT, src,
    //                                       make_const_int(src->type_, 0), bb, true);
    //         bb->add_instruction_before_inst(positive, inst);

    //         auto *match = new BinaryInst(ty, Instruction::And, positive, odd, bb, true);
    //         stampIntegerFacts(match);
    //         bb->add_instruction_before_inst(match, inst);

    //         if (pred == ICmpInst::ICMP_EQ)
    //             return match;

    //         auto *negated = new BinaryInst(ty, Instruction::Xor, match,
    //                                        make_const_int(ty, 1), bb, true);
    //         stampIntegerFacts(negated);
    //         bb->add_instruction_before_inst(negated, inst);
    //         return negated;
    //     }
    // }

    // 5. Fold add/sub with constant into the comparison
    return foldICmpAddSub(inst);
}

// ═══════════════════════════════════════════════════════════════════════
// visitFCmp  —  floating-point comparison simplifications
// ═══════════════════════════════════════════════════════════════════════

Value* visitFCmp(FCmpInst *inst) {
    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;  // i1

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    // Constant fold: evaluate at compile time (ordered/unordered share the
    // same result for finite constants; NaN-producing constants do not occur).
    if (cx && cy) {
        float a = cx->value_, b = cy->value_;
        bool result;
        switch (inst->fcmp_op_) {
            case FCmpInst::FCMP_OEQ: case FCmpInst::FCMP_UEQ: result = (a == b); break;
            case FCmpInst::FCMP_ONE: case FCmpInst::FCMP_UNE: result = (a != b); break;
            case FCmpInst::FCMP_OGT: case FCmpInst::FCMP_UGT: result = (a >  b); break;
            case FCmpInst::FCMP_OGE: case FCmpInst::FCMP_UGE: result = (a >= b); break;
            case FCmpInst::FCMP_OLT: case FCmpInst::FCMP_ULT: result = (a <  b); break;
            case FCmpInst::FCMP_OLE: case FCmpInst::FCMP_ULE: result = (a <= b); break;
            default: return nullptr;
        }
        return make_const_int(ty, result ? 1 : 0);
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
