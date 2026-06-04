#pragma once
#include "../../../include/mid/ir/ir.hpp"

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

// ── Value analysis ────────────────────────────────────────────────────
//
// These queries answer "what can we prove about a Value?" by inspecting
// its definition.  When a simplification has a precondition (e.g. "x ≥ 0")
// the guard returns false if we cannot prove the condition, keeping the
// transform safe.  As analysis improves (range analysis, loop induction
// variables, etc.) these functions naturally cover more cases.

// Can we prove v is always non-negative (≥ 0)?
inline bool isKnownNonNegative(Value *v) {
    if (auto *ci = dynamic_cast<ConstantInt*>(v))
        return ci->value_ >= 0;

    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst) return false;

    // lshr always produces a non-negative result (zero-extended).
    if (inst->op_id_ == Instruction::LShr)
        return true;

    // and with a mask whose sign bit is 0 clears any sign.
    if (inst->op_id_ == Instruction::And) {
        auto *mask = dynamic_cast<ConstantInt*>(inst->get_operand(1));
        if (!mask) mask = dynamic_cast<ConstantInt*>(inst->get_operand(0));
        if (mask) {
            unsigned bits = static_cast<IntegerType*>(inst->type_)->num_bits_;
            if (mask->value_ >= 0 && (mask->value_ & (1 << (bits - 1))) == 0)
                return true;
        }
    }

    // zext result is always non-negative.
    if (inst->op_id_ == Instruction::ZExt)
        return true;

    return false;
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

// Can we prove the lower `k` bits of v are all zero?
// (i.e. v is a multiple of 2^k).
// When `ctx` is provided we also inspect dominating branch conditions.
inline bool isKnownMultipleOf(Value *v, int k, BasicBlock *ctx = nullptr) {
    if (k <= 0) return true;
    int mask = (1 << k) - 1;

    // Constant case.
    if (auto *ci = dynamic_cast<ConstantInt*>(v))
        return (ci->value_ & mask) == 0;

    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst)
        return ctx ? isKnownMultipleOfFromBranch(v, k, ctx) : false;

    // shl y, n  →  lower n bits are zero.
    if (inst->op_id_ == Instruction::Shl) {
        auto *amt = dynamic_cast<ConstantInt*>(inst->get_operand(1));
        if (amt && amt->value_ >= k) return true;
    }

    // mul y, C  →  if C is a multiple of 2^k, result is too.
    if (inst->op_id_ == Instruction::Mul) {
        auto *c1 = dynamic_cast<ConstantInt*>(inst->get_operand(0));
        auto *c2 = dynamic_cast<ConstantInt*>(inst->get_operand(1));
        if ((c1 && (c1->value_ & mask) == 0) ||
            (c2 && (c2->value_ & mask) == 0))
            return true;
    }

    // and y, M  →  if M has lower k bits zero, result does too.
    if (inst->op_id_ == Instruction::And) {
        auto *amask = dynamic_cast<ConstantInt*>(inst->get_operand(1));
        if (!amask) amask = dynamic_cast<ConstantInt*>(inst->get_operand(0));
        if (amask && (amask->value_ & mask) == 0) return true;
    }

    // Also try branch-condition analysis.
    if (ctx) return isKnownMultipleOfFromBranch(v, k, ctx);
    return false;
}

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
