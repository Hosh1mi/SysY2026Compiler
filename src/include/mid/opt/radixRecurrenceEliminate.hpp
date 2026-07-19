#pragma once

#include "pass.hpp"

// Eliminate a pure radix-2 self recurrence of the form
//
//   F(a, 0) = 0
//   F(a, 1) = a srem M
//   F(a, b) = (2 * F(a, b / 2)) srem M,
//              optionally followed by `(result + a) srem M` when b % 2 == 1.
//
// The replacement walks the positive bits of b from most significant to least
// significant and performs the original i32 add/srem operations in the same
// order.  Negative b values retain the source recurrence's result of zero.
// Recognition is structural: function names, call sites, and the modulus value
// are not used as activation signals.
class RadixRecurrenceEliminate : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "RadixRecurrenceEliminate"; }
};
