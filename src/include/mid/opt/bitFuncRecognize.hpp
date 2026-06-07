#pragma once
// BitFuncRecognize — recognize bitwise-op emulations via per-bit abstract
// interpretation, then rewrite call sites with the equivalent IR op.
//
// Replaces the temporary BitFuncSubstitute pass.  Unlike BitFuncSubstitute,
// this pass does NOT key on function names.  It analyzes each candidate
// function's IR body, tracking a 32-bit symbolic expression for every i32
// SSA value, and matches the return value's bit-vector against closed-form
// patterns (AND / OR / XOR / SHL / parametric SHL / parametric LSHR).
//
// Design: docs/BITOP_LOWERING.md §4.
//
// Scope:
//   - Abstract domain: BitExpr (hash-consed) and BitVec[32] over inputs.
//   - Transfer functions: const, copy, mul/sdiv by const power-of-two,
//     bit-wise AND/OR/XOR (via IR), select on i1, phi (merge).
//   - Loop handling: constant trip count via symbolic unrolling.
//   - Recognizer: AND/OR/XOR of corresponding input bits, constant SHL,
//     parametric (variable-shift) SHL and LSHR (rotlN / rotrN style).
//
// Known limitations:
//   - Constant LSHR is *not* recognized.  At the bit-vector level the source
//     IR for a shape `bv[i] = BitOf(X, i+k)` is ambiguous: it could be a
//     `sdiv x, 1<<k` from SysY `x/c`, or a hand-written bit assembly of an
//     unsigned `x>>k`.  The two sources disagree on negative x, and the
//     analysis cannot tell them apart, so rewriting either way is unsound.
//     Constant SHL is safe because `x*c` and a bit-assembly `x<<k` agree.
//   - Single-threaded only: the hash-consing arena is a translation-unit
//     static map cleared per execute() invocation.
//   - Argument count limited to 1..3 (matches the recognized patterns'
//     parameter shapes); functions with more args are skipped.
//
// Future work:
//   - Resolve LSHR ambiguity by carrying a source-op tag through the
//     abstraction so the recognizer can choose lshr vs sdiv on rewrite.
//   - General add/sub transfer with full-adder carry tracking (partial today).
//   - Cross-function inlining-aware analysis.

#include "pass.hpp"

class BitFuncRecognize : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "BitFuncRecognize"; }
};
