#pragma once
// BitFuncRecognize — recognize bitwise-op emulations via per-bit abstract
// interpretation, then rewrite call sites with the equivalent IR op.
//
// Replaces the temporary BitFuncSubstitute pass. Unlike BitFuncSubstitute,
// this pass does NOT key on function names. It analyzes each candidate
// function's IR body, tracking a 32-bit symbolic expression for every
// i32 SSA value, and matches the return value's bit-vector against
// closed-form patterns (AND/OR/XOR/Shift).
//
// Design: docs/BITOP_LOWERING.md §4.
//
// Current scope (iteration 1):
//   - Abstract domain: BitExpr (hash-consed) and BitVec[32] over inputs.
//   - Transfer functions for: const, copy, mul/sdiv by const power-of-two,
//     bit-wise AND/OR/XOR (via IR), select on i1, phi (merge).
//   - Loop handling: constant trip count via symbolic unrolling.
//   - Recognizer: AND/OR/XOR of corresponding input bits; logical shifts.
//
// Future work (later iterations):
//   - Variable trip count via SCEV-style induction.
//   - General add/sub transfer with full-adder carry tracking.
//   - Cross-function inlining-aware analysis.

#include "pass.hpp"

class BitFuncRecognize : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "BitFuncRecognize"; }
};
