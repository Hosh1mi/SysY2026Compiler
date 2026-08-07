#pragma once
#include "../../../include/mid/ir/ir.hpp"
#include "../../../include/mid/analysis/constantEvaluator.hpp"
#include "../../../include/mid/analysis/rangeAnalysis.hpp"
#include "../../../include/mid/analysis/valueFacts.hpp"

#include <cstddef>

// ── Constant helpers ──────────────────────────────────────────────────

inline ConstantInt* as_const_int(Value* v) {
    return dynamic_cast<ConstantInt*>(v);
}

inline ConstantFloat* as_const_float(Value* v) {
    return dynamic_cast<ConstantFloat*>(v);
}

inline bool is_constant(Value* v) {
    return as_const_int(v) || as_const_float(v);
}

// ── Constant creators ─────────────────────────────────────────────────

inline ConstantInt* make_const_int(Type* ty, int v) {
    return new ConstantInt(ty, v);
}

inline ConstantFloat* make_const_float(Type* ty, float v) {
    return new ConstantFloat(ty, v);
}

inline bool sameConstantValue(Value *lhs, Value *rhs) {
    if (lhs == rhs) return true;
    auto *li = dynamic_cast<ConstantInt *>(lhs);
    auto *ri = dynamic_cast<ConstantInt *>(rhs);
    if (li && ri) return li->value_ == ri->value_;
    auto *lf = dynamic_cast<ConstantFloat *>(lhs);
    auto *rf = dynamic_cast<ConstantFloat *>(rhs);
    if (lf && rf) return lf->value_ == rf->value_;
    return dynamic_cast<ConstantZero *>(lhs) && dynamic_cast<ConstantZero *>(rhs);
}

inline bool sameInstructionShape(Instruction *lhs, Instruction *rhs) {
    if (!lhs || !rhs) return false;
    if (lhs->op_id_ != rhs->op_id_ || lhs->num_ops_ != rhs->num_ops_)
        return false;
    if (lhs->type_ != rhs->type_) return false;
    if (lhs->sem_flags_ != rhs->sem_flags_) return false;
    if (auto *lc = dynamic_cast<ICmpInst *>(lhs)) {
        auto *rc = dynamic_cast<ICmpInst *>(rhs);
        if (!rc || lc->icmp_op_ != rc->icmp_op_) return false;
    }
    if (auto *lc = dynamic_cast<FCmpInst *>(lhs)) {
        auto *rc = dynamic_cast<FCmpInst *>(rhs);
        if (!rc || lc->fcmp_op_ != rc->fcmp_op_) return false;
    }
    for (unsigned i = 0; i < lhs->num_ops_; ++i) {
        if (!sameConstantValue(lhs->get_operand(i), rhs->get_operand(i)))
            return false;
    }
    return true;
}

// ── Power-of-two helpers ──────────────────────────────────────────────

inline bool isPowerOfTwo(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

inline int log2Int(int v) {
    int r = 0;
    while (v >>= 1) ++r;
    return r;
}

// 能否证明 v = 2^k？如能证明则设置 k 并返回 true。
inline bool isKnownPowerOfTwo(Value *v, int &k) {
    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        if (ci->value_ > 0 && isPowerOfTwo(ci->value_)) {
            k = log2Int(ci->value_);
            return true;
        }
        return false;
    }

    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst) return false;

    // shl base, n — if base is a known power of 2, then result is 2^(k_base + n)
    if (inst->op_id_ == Instruction::Shl) {
        auto *amt = dynamic_cast<ConstantInt*>(inst->get_operand(1));
        if (!amt || amt->value_ < 0) return false;
        int baseK;
        if (isKnownPowerOfTwo(inst->get_operand(0), baseK)) {
            int resultK = baseK + amt->value_;
            if (resultK >= 31) return false;
            k = resultK;
            return true;
        }
        return false;
    }

    return false;
}

// ── Value analysis ────────────────────────────────────────────────────
//
// These queries answer "what can we prove about a Value?" by inspecting
// its definition.  When a simplification has a precondition (e.g. "x ≥ 0")
// the guard returns false if we cannot prove the condition, keeping the
// transform safe.  As analysis improves (range analysis, loop induction
// variables, etc.) these functions naturally cover more cases.

extern thread_local DominatorTreeAnalysis *gInstCombineDominatorTree;
inline bool isKnownNonNegative(Value *value, BasicBlock *context = nullptr) {
    return ValueFacts::isKnownNonNegative(value, context,
                                          gInstCombineDominatorTree);
}
using ValueFacts::isKnownMultipleOf;
using ValueFacts::knownAbsBound;

inline void copySemFlags(Value *from, Value *to) {
    if (from && to)
        to->copySemFlagsFrom(from);
}

inline bool setSemFlagIfMissing(Value *v, SemFlag flag) {
    if (!v || v->hasSemFlag(flag))
        return false;
    v->setSemFlag(flag);
    return true;
}

inline bool operandsAreDisjointBits(Value *lhs, Value *rhs, BasicBlock *ctx) {
    auto *cr = dynamic_cast<ConstantInt *>(rhs);
    if (cr && cr->value_ > 0) {
        int k = 1;
        while (k < 31 && (1 << k) <= cr->value_) ++k;
        return isKnownMultipleOf(lhs, k, ctx);
    }
    auto *cl = dynamic_cast<ConstantInt *>(lhs);
    if (cl && cl->value_ > 0) {
        int k = 1;
        while (k < 31 && (1 << k) <= cl->value_) ++k;
        return isKnownMultipleOf(rhs, k, ctx);
    }
    return false;
}

inline void stampOrDisjoint(BinaryInst *inst) {
    if (!inst || inst->op_id_ != Instruction::Or) return;
    if (operandsAreDisjointBits(inst->get_operand(0), inst->get_operand(1),
                                inst->parent_))
        inst->setSemFlag(SemFlag::Disjoint);
}

inline void stampAshrExact(BinaryInst *inst) {
    if (!inst || inst->op_id_ != Instruction::AShr) return;
    auto *shift = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!shift || shift->value_ <= 0) return;
    if (isKnownMultipleOf(inst->get_operand(0), shift->value_, inst->parent_))
        inst->setSemFlag(SemFlag::Exact);
}

inline bool canProveAddOneNoWrap(BinaryInst *inst) {
    if (!inst || inst->op_id_ != Instruction::Add || !inst->parent_)
        return false;
    auto *c = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!c || c->value_ != 1) return false;

    auto *bb = inst->parent_;
    auto checkCmp = [&](ICmpInst *cmp) -> bool {
        if (!cmp) return false;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SLT && cmp->get_operand(0) == inst->get_operand(0))
            return true;
        if (cmp->icmp_op_ == ICmpInst::ICMP_SGT && cmp->get_operand(1) == inst->get_operand(0))
            return true;
        return false;
    };
    for (auto *pred : bb->pre_bbs_) {
        auto *predTerm = dynamic_cast<BranchInst *>(pred->get_terminator());
        if (!predTerm || predTerm->num_ops_ != 3 || predTerm->get_operand(1) != bb)
            continue;
        if (checkCmp(dynamic_cast<ICmpInst *>(predTerm->get_operand(0))))
            return true;
    }
    return false;
}

inline void stampAddNoWrap(BinaryInst *inst) {
    if (!inst || inst->op_id_ != Instruction::Add) return;
    auto *rhs = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!rhs || rhs->value_ != 1) return;
    if (!canProveAddOneNoWrap(inst))
        return;
    inst->setSemFlag(SemFlag::NoSignedWrap);
    if (isKnownNonNegative(inst->get_operand(0), inst->parent_))
        inst->setSemFlag(SemFlag::NoUnsignedWrap);
}

inline void stampIntegerFacts(BinaryInst *inst) {
    if (!inst || inst->type_->tid_ != Type::IntegerTyID) return;
    switch (inst->op_id_) {
    case Instruction::Add:
        stampAddNoWrap(inst);
        break;
    case Instruction::AShr:
        stampAshrExact(inst);
        break;
    case Instruction::Or:
        stampOrDisjoint(inst);
        break;
    default:
        break;
    }
}

inline void stampIntegerFacts(Instruction *inst) {
    stampIntegerFacts(dynamic_cast<BinaryInst *>(inst));
}

// Look through dominating branch conditions to prove v is a multiple of C.
// If this BB is reached via  br (icmp eq (srem v, C), 0), this_bb, other
// then v % C == 0, i.e. v is a multiple of C.
// Returns true when C is itself a multiple of 2^k.
inline bool isKnownMultipleOfFromBranch(Value *v, int k, BasicBlock *ctx) {
    int mask = (1 << k) - 1;
    for (auto *pred : ctx->pre_bbs_) {
        auto *br = pred->get_terminator();
        if (!br || br->op_id_ != Instruction::Br || br->num_ops_ < 3)
            continue;
        auto *cond = dynamic_cast<ICmpInst*>(br->get_operand(0));
        if (!cond || cond->icmp_op_ != ICmpInst::ICMP_EQ) continue;
        // BB must be reached when condition is true.
        if (br->get_operand(1) != ctx) continue;
        // Pattern: icmp eq (srem v, C), 0
        auto *srem = dynamic_cast<Instruction*>(cond->get_operand(0));
        if (!srem || srem->op_id_ != Instruction::SRem) continue;
        if (srem->get_operand(0) != v) continue;
        auto *zero = dynamic_cast<ConstantInt*>(cond->get_operand(1));
        if (!zero || zero->value_ != 0) continue;
        auto *C = dynamic_cast<ConstantInt*>(srem->get_operand(1));
        if (!C) continue;
        // v is a multiple of C.  If C itself is a multiple of 2^k, so is v.
        if ((C->value_ & mask) == 0) return true;
    }
    return false;
}

// // Can we prove the lower `k` bits of v are all zero?
// // (i.e. v is a multiple of 2^k).
// // When `ctx` is provided we also inspect dominating branch conditions.
// inline bool isKnownMultipleOf(Value *v, int k, BasicBlock *ctx = nullptr) {
//     if (k <= 0) return true;
//     int mask = (1 << k) - 1;

//     // Constant case.
//     if (auto *ci = dynamic_cast<ConstantInt*>(v))
//         return (ci->value_ & mask) == 0;

//     auto *inst = dynamic_cast<Instruction*>(v);
//     if (!inst)
//         return ctx ? isKnownMultipleOfFromBranch(v, k, ctx) : false;

//     // shl y, n  →  lower n bits are zero.
//     if (inst->op_id_ == Instruction::Shl) {
//         auto *amt = dynamic_cast<ConstantInt*>(inst->get_operand(1));
//         if (amt && amt->value_ >= k) return true;
//     }

//     // mul y, C  →  if C is a multiple of 2^k, result is too.
//     if (inst->op_id_ == Instruction::Mul) {
//         auto *c1 = dynamic_cast<ConstantInt*>(inst->get_operand(0));
//         auto *c2 = dynamic_cast<ConstantInt*>(inst->get_operand(1));
//         if ((c1 && (c1->value_ & mask) == 0) ||
//             (c2 && (c2->value_ & mask) == 0))
//             return true;
//     }

//     // and y, M  →  if M has lower k bits zero, result does too.
//     if (inst->op_id_ == Instruction::And) {
//         auto *amask = dynamic_cast<ConstantInt*>(inst->get_operand(1));
//         if (!amask) amask = dynamic_cast<ConstantInt*>(inst->get_operand(0));
//         if (amask && (amask->value_ & mask) == 0) return true;
//     }

// // Also try branch-condition analysis.
//     if (ctx) return isKnownMultipleOfFromBranch(v, k, ctx);
//     return false;
// }

extern thread_local RangeAnalysis *gInstCombineRangeAnalysis;

// ── Per-opcode visit functions ────────────────────────────────────────
// Each returns a replacement Value*, or nullptr if no simplification.

// AddSub
Value* visitAdd(BinaryInst *inst);
Value* visitSub(BinaryInst *inst);
Value* visitFAdd(BinaryInst *inst);
Value* visitFSub(BinaryInst *inst);
Value* visitFNeg(UnaryInst *inst);

// MulDivRem
Value* visitMul(BinaryInst *inst);
Value* visitSDiv(BinaryInst *inst);
Value* visitSRem(BinaryInst *inst);
Value* visitFMul(BinaryInst *inst);
Value* visitFDiv(BinaryInst *inst);

// Shifts
Value* visitShl(BinaryInst *inst);
Value* visitLShr(BinaryInst *inst);
Value* visitAShr(BinaryInst *inst);

// Bitwise
Value* visitAnd(BinaryInst *inst);
Value* visitOr(BinaryInst *inst);
Value* visitXor(BinaryInst *inst);

// CmpSelect
Value* visitICmp(ICmpInst *inst);
Value* visitFCmp(FCmpInst *inst);
Value* visitSelect(SelectInst *inst);
ICmpInst::ICmpOp getSwappedPredicate(ICmpInst::ICmpOp op);
Value* foldICmpAddSub(ICmpInst *inst);

// Cast / Phi
// visitCast: ZExt/SItoFP/FPtoSI/Clz/BitCast identity + constant fold
// visitPhi: collapse when all non-self incomings are the same value
Value* visitCast(Instruction *inst);
Value* visitPhi(PhiInst *inst);

// First-class vector values.  These combines deliberately operate on the IR
// shape rather than on any source spelling, so they remain useful when the
// language grammar eventually chooses its VecType syntax.
Value* visitVectorBinary(BinaryInst *inst);
Value* visitInsertElement(InsertElementInst *inst);
Value* visitExtractElement(ExtractElementInst *inst);
Value* visitShuffleVector(ShuffleVectorInst *inst);
