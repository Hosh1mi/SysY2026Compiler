#pragma once
// GlobalScalarPromotion — replace direct loads/stores of scalar (integer)
// globals with loads/stores of a function-local alloca that mirrors the
// global's value across the function's lifetime.  A subsequent Mem2Reg lifts
// the alloca into SSA, removing memory traffic from the hot path.
//
// Scope and limitations:
//   - Runs only on functions whose surviving calls are all `FnPure`.  A
//     readonly/unknown call may still observe the same global through another
//     function, and promoting locally would then break what the callee sees.
//   - Integer-typed globals only.  Float globals and arrays are skipped.
//   - Each ret block grows two instructions per promoted global (load alloca
//     + store global) so the observable value at return matches the
//     unpromoted program.
//
// Pipeline order: must run after InlineExpand (so `main` is call-free) and
// before Mem2Reg (which SSA-promotes the inserted allocas).
#include "pass.hpp"
class GlobalScalarPromotion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "GlobalScalarPromotion"; }
};
