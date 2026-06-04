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

// ── Per-opcode visit functions ────────────────────────────────────────
// Each returns a replacement Value*, or nullptr if no simplification.

Value* visitAdd(BinaryInst *inst);
Value* visitSub(BinaryInst *inst);
Value* visitFAdd(BinaryInst *inst);
Value* visitFSub(BinaryInst *inst);
Value* visitFNeg(UnaryInst *inst);
