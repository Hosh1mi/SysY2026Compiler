#pragma once
#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

// LoopSimplify: canonicalize loops for downstream loop passes.
//   - Inserts a dedicated preheader for every natural loop that doesn't already
//     have one (header has >1 predecessor or the sole outside predecessor is not
//     a clean unconditional-branch block).
//   - Updates phi nodes in the header to reference the new preheader.
//
// This should run early in the loop pipeline, before LICM / LoopUnroll /
// LoopVectorize / IndVarStrengthReduce, so those passes can rely on preheaders.

class LoopSimplify : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopSimplify"; }

private:
    bool runOnFunction(Function *func);
    bool insertPreheader(Loop *loop, Function *func);
};
