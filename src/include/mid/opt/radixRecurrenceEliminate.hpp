#pragma once

#include "pass.hpp"

// Eliminate a pure radix-2 self recurrence of the form
//
//   F(a, 0) = 0
//   F(a, 1) = a srem M
//   F(a, b) = (2 * F(a, b / 2)) srem M,
//              optionally followed by `(result + a) srem M` when b % 2 == 1.
//
// When the matched i32 additions are provably overflow-free, F(a, b) equals
// (a * b) srem M and is lowered to the MulMod intrinsic.  Other positive-b
// inputs use a bit-walking fallback that preserves the original i32 operation
// order; non-positive b values retain the source result of zero.  Recognition
// is structural: function names and call sites are not activation signals.
class RadixRecurrenceEliminate : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "RadixRecurrenceEliminate"; }
};
