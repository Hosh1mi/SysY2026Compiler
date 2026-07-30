#include "instCombineInternal.hpp"
#include "../../../include/mid/ir/intrinsics.hpp"

#include <limits>

// ═══════════════════════════════════════════════════════════════════════
// getSwappedPredicate — icmp predicate after swapping operands
// ═══════════════════════════════════════════════════════════════════════

ICmpInst::ICmpOp getSwappedPredicate(ICmpInst::ICmpOp op) {
    switch (op) {
        case ICmpInst::ICMP_EQ:  return ICmpInst::ICMP_EQ;
        case ICmpInst::ICMP_NE:  return ICmpInst::ICMP_NE;
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

FCmpInst::FCmpOp getSwappedFPredicate(FCmpInst::FCmpOp op) {
    switch (op) {
        case FCmpInst::FCMP_OEQ: case FCmpInst::FCMP_UEQ: return op;
        case FCmpInst::FCMP_ONE: case FCmpInst::FCMP_UNE: return op;
        case FCmpInst::FCMP_OGT: return FCmpInst::FCMP_OLT;
        case FCmpInst::FCMP_OGE: return FCmpInst::FCMP_OLE;
        case FCmpInst::FCMP_OLT: return FCmpInst::FCMP_OGT;
        case FCmpInst::FCMP_OLE: return FCmpInst::FCMP_OGE;
        case FCmpInst::FCMP_UGT: return FCmpInst::FCMP_ULT;
        case FCmpInst::FCMP_UGE: return FCmpInst::FCMP_ULE;
        case FCmpInst::FCMP_ULT: return FCmpInst::FCMP_UGT;
        case FCmpInst::FCMP_ULE: return FCmpInst::FCMP_UGE;
        default: return op;
    }
}

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

bool inferSignedLowerBound(Value *value, BasicBlock *ctx,
                           long long &lower, int depth) {
    if (!value || depth > 8)
        return false;
    if (auto *constant = as_const_int(value)) {
        lower = constant->value_;
        return true;
    }
    if (ValueFacts::isKnownNonNegative(value, ctx)) {
        lower = 0;
        return true;
    }

    auto *select = dynamic_cast<SelectInst *>(value);
    SignedMinMaxIntrinsic kind;
    Value *lhs = nullptr;
    Value *rhs = nullptr;
    if (!select ||
        !matchSignedMinMaxSelect(select, kind, lhs, rhs) ||
        kind != SignedMinMaxIntrinsic::SMax)
        return false;

    Value *base = nullptr;
    BinaryInst *complement = nullptr;
    if (auto *sub = dynamic_cast<BinaryInst *>(lhs);
        sub && sub->op_id_ == Instruction::Sub &&
        sub->get_operand(1) == rhs &&
        as_const_int(sub->get_operand(0))) {
        base = rhs;
        complement = sub;
    } else if (auto *sub = dynamic_cast<BinaryInst *>(rhs);
               sub && sub->op_id_ == Instruction::Sub &&
               sub->get_operand(1) == lhs &&
               as_const_int(sub->get_operand(0))) {
        base = lhs;
        complement = sub;
    }

    long long baseLower = 0;
    if (base && complement &&
        inferSignedLowerBound(base, ctx, baseLower, depth + 1)) {
        const long long c =
            as_const_int(complement->get_operand(0))->value_;
        const long long typeMin = std::numeric_limits<int>::min();
        const long long typeMax = std::numeric_limits<int>::max();
        if (c - typeMax >= typeMin &&
            c - baseLower <= typeMax) {
            const long long ceilHalf =
                c >= 0 ? (c + 1) / 2 : c / 2;
            lower = std::max(baseLower, ceilHalf);
            return true;
        }
    }

    long long lhsLower = 0;
    long long rhsLower = 0;
    if (!inferSignedLowerBound(lhs, ctx, lhsLower, depth + 1) ||
        !inferSignedLowerBound(rhs, ctx, rhsLower, depth + 1))
        return false;
    lower = std::max(lhsLower, rhsLower);
    return true;
}

// Dominated false-edge: if k1*x < B failed and x≥0 and k2≥k1, then k2*x < B is false.
// Relies on signed overflow being undefined at source level (SysY / C signed arith).
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

        auto *falseSucc = dynamic_cast<BasicBlock *>(br->get_operand(2));
        if (falseSucc == bb && cur.scale >= prev.scale)
            return make_const_int(inst->type_, 0);
    }

    return nullptr;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// visitICmp — integer comparison
//
// Capabilities:
//   - Constant fold all predicates; canonicalize constant to RHS
//   - Self-compare: eq/ge/le → true, ne/gt/lt → false
//   - Dominated scaled SLT fold (non-negative base, larger scale → false)
//   - foldICmpAddSub: fold add/sub±C into predicate / RHS constant
// ═══════════════════════════════════════════════════════════════════════

Value* visitICmp(ICmpInst *inst) {
    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;
    ICmpInst::ICmpOp pred = inst->icmp_op_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

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

    if (cx && !cy) {
        ICmpInst::ICmpOp swapped = getSwappedPredicate(pred);
        auto *new_inst = new ICmpInst(swapped, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (x == y) {
        switch (pred) {
            case ICmpInst::ICMP_EQ:
            case ICmpInst::ICMP_SGE:
            case ICmpInst::ICMP_SLE:
            case ICmpInst::ICMP_UGE:
            case ICmpInst::ICMP_ULE:
                return make_const_int(ty, 1);
            case ICmpInst::ICMP_NE:
            case ICmpInst::ICMP_SGT:
            case ICmpInst::ICMP_SLT:
            case ICmpInst::ICMP_UGT:
            case ICmpInst::ICMP_ULT:
                return make_const_int(ty, 0);
            default:
                return nullptr;
        }
    }

    if (cy) {
        if (ValueFacts::isKnownNonNegative(x, bb)) {
            bool known = false;
            bool result = false;
            switch (pred) {
            case ICmpInst::ICMP_SGT:
                if (cy->value_ < 0) known = result = true;
                break;
            case ICmpInst::ICMP_SGE:
                if (cy->value_ <= 0) known = result = true;
                break;
            case ICmpInst::ICMP_SLT:
                if (cy->value_ <= 0) known = true;
                break;
            case ICmpInst::ICMP_SLE:
                if (cy->value_ < 0) known = true;
                break;
            default:
                break;
            }
            if (known)
                return make_const_int(ty, result ? 1 : 0);
        }

        uint32_t unsignedLower = 0, unsignedUpper = 0;
        if (ValueFacts::knownUnsignedBounds(x, unsignedLower, unsignedUpper)) {
            const unsigned bits = ValueFacts::integerBitWidth(x);
            const uint32_t mask = ValueFacts::widthMask(bits);
            const uint32_t constant =
                static_cast<uint32_t>(cy->value_) & mask;
            bool known = false;
            bool result = false;
            switch (pred) {
            case ICmpInst::ICMP_EQ:
                known = constant < unsignedLower || constant > unsignedUpper;
                result = false;
                break;
            case ICmpInst::ICMP_NE:
                known = constant < unsignedLower || constant > unsignedUpper;
                result = true;
                break;
            case ICmpInst::ICMP_UGT:
                if (unsignedLower > constant) known = result = true;
                else if (unsignedUpper <= constant) known = true;
                break;
            case ICmpInst::ICMP_UGE:
                if (unsignedLower >= constant) known = result = true;
                else if (unsignedUpper < constant) known = true;
                break;
            case ICmpInst::ICMP_ULT:
                if (unsignedUpper < constant) known = result = true;
                else if (unsignedLower >= constant) known = true;
                break;
            case ICmpInst::ICMP_ULE:
                if (unsignedUpper <= constant) known = result = true;
                else if (unsignedLower > constant) known = true;
                break;
            default:
                break;
            }
            if (known)
                return make_const_int(ty, result ? 1 : 0);
        }

        int64_t signedLower = 0, signedUpper = 0;
        if (ValueFacts::knownNonNegativeBounds(x, signedLower, signedUpper)) {
            const int64_t constant = cy->value_;
            bool known = false;
            bool result = false;
            switch (pred) {
            case ICmpInst::ICMP_SGT:
                if (signedLower > constant) known = result = true;
                else if (signedUpper <= constant) known = true;
                break;
            case ICmpInst::ICMP_SGE:
                if (signedLower >= constant) known = result = true;
                else if (signedUpper < constant) known = true;
                break;
            case ICmpInst::ICMP_SLT:
                if (signedUpper < constant) known = result = true;
                else if (signedLower >= constant) known = true;
                break;
            case ICmpInst::ICMP_SLE:
                if (signedUpper <= constant) known = result = true;
                else if (signedLower > constant) known = true;
                break;
            default:
                break;
            }
            if (known)
                return make_const_int(ty, result ? 1 : 0);
        }
    }

    if (auto *folded = foldScaledCompareFromPred(inst))
        return folded;

    // Keep the canonical compare feeding a signed min/max select intact.
    // Rewriting x < C-x into a scaled compare before visiting the select
    // obscures both the min/max identity and its range-derived simplification.
    for (const Use &use : inst->use_list_) {
        auto *select = dynamic_cast<SelectInst *>(use.val_);
        SignedMinMaxIntrinsic kind;
        Value *lhs = nullptr;
        Value *rhs = nullptr;
        if (select &&
            matchSignedMinMaxSelect(select, kind, lhs, rhs))
            return nullptr;
    }

    return foldICmpAddSub(inst);
}

// ═══════════════════════════════════════════════════════════════════════
// visitFCmp — floating-point comparison
//
// Capabilities:
//   - Constant fold ordered/unordered predicates on finite constants
//   - Canonicalize constant to RHS (swap predicate)
//   - No self-compare fold: NaN makes x==x false under ordered predicates
// ═══════════════════════════════════════════════════════════════════════

Value* visitFCmp(FCmpInst *inst) {
    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

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

    if (cx && !cy) {
        FCmpInst::FCmpOp swapped = getSwappedFPredicate(inst->fcmp_op_);
        auto *new_inst = new FCmpInst(swapped, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSelect — select
//
// Capabilities:
//   - Constant cond → chosen arm; identical arms → that value
//   - Boolean canonicalize (i32): select c,1,0 → zext c
//                                 select c,0,1 → zext (xor c,1)
// ═══════════════════════════════════════════════════════════════════════

Value* visitSelect(SelectInst *inst) {
    Value *cond  = inst->get_operand(0);
    Value *tval  = inst->get_operand(1);
    Value *fval  = inst->get_operand(2);
    Type  *ty    = inst->type_;
    BasicBlock *bb = inst->parent_;

    auto *cc = as_const_int(cond);
    if (cc) {
        if (cc->value_ != 0) return tval;
        return fval;
    }

    if (tval == fval)
        return tval;

    SignedMinMaxIntrinsic minMaxKind;
    Value *minMaxLHS = nullptr;
    Value *minMaxRHS = nullptr;
    const bool isSignedMinMax =
        matchSignedMinMaxSelect(inst, minMaxKind, minMaxLHS, minMaxRHS);
    if (isSignedMinMax &&
        minMaxKind == SignedMinMaxIntrinsic::SMax) {
        Value *base = nullptr;
        BinaryInst *complement = nullptr;
        if (auto *sub = dynamic_cast<BinaryInst *>(minMaxLHS);
            sub && sub->op_id_ == Instruction::Sub &&
            sub->get_operand(1) == minMaxRHS &&
            dynamic_cast<ConstantInt *>(sub->get_operand(0))) {
            base = minMaxRHS;
            complement = sub;
        } else if (auto *sub = dynamic_cast<BinaryInst *>(minMaxRHS);
                   sub && sub->op_id_ == Instruction::Sub &&
                   sub->get_operand(1) == minMaxLHS &&
                   dynamic_cast<ConstantInt *>(sub->get_operand(0))) {
            base = minMaxLHS;
            complement = sub;
        }

        if (base && complement) {
            RangeAnalysis::IntRange baseRange =
                gInstCombineRangeAnalysis
                    ? gInstCombineRangeAnalysis->getRange(base, bb)
                    : RangeAnalysis::IntRange::top();
            auto *constant = static_cast<ConstantInt *>(
                complement->get_operand(0));
            auto *integerTy = dynamic_cast<IntegerType *>(ty);
            long long baseLower = 0;
            bool hasLower =
                baseRange.valid && !baseRange.isTop &&
                !baseRange.isBottom;
            if (hasLower)
                baseLower = baseRange.lower;
            else
                hasLower =
                    inferSignedLowerBound(base, bb, baseLower, 0);
            if (integerTy && integerTy->num_bits_ == 32 && hasLower) {
                const long long c = constant->value_;
                const long long complementMin =
                    c - std::numeric_limits<int>::max();
                const long long complementMax = c - baseLower;
                if (complementMin >= std::numeric_limits<int>::min() &&
                    complementMax <= std::numeric_limits<int>::max() &&
                    baseLower >= complementMax)
                    return base;
            }
        }
    }

    if (dynamic_cast<VectorType *>(ty) &&
        isSignedMinMax) {
        auto *function = getOrInsertSignedMinMaxIntrinsic(
            bb->parent_->parent_, minMaxKind, ty);
        if (function) {
            auto *call = new CallInst(function, {minMaxLHS, minMaxRHS}, bb,
                                      true);
            bb->add_instruction_before_inst(call, inst);
            return call;
        }
    }

    if (ty->tid_ == Type::IntegerTyID) {
        auto *ct = as_const_int(tval);
        auto *cf = as_const_int(fval);
        if (ct && cf && ct->value_ == 1 && cf->value_ == 0) {
            auto *zext = new ZextInst(Instruction::ZExt, cond, ty, bb, true);
            bb->add_instruction_before_inst(zext, inst);
            return zext;
        }
        if (ct && cf && ct->value_ == 0 && cf->value_ == 1) {
            auto *xored = new BinaryInst(cond->type_, Instruction::Xor, cond,
                                         make_const_int(cond->type_, 1), bb, true);
            stampIntegerFacts(xored);
            bb->add_instruction_before_inst(xored, inst);
            auto *zext = new ZextInst(Instruction::ZExt, xored, ty, bb, true);
            bb->add_instruction_before_inst(zext, inst);
            return zext;
        }
    }

    return nullptr;
}
