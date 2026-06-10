#pragma once
#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

// LoopSimplify: canonicalize loops for downstream loop passes.
//   - Inserts a dedicated preheader for every natural loop that doesn't already
//     have one.
//   - Merges multiple backedges through one dedicated backedge block.
//   - Splits exit blocks with out-of-loop predecessors so every exit is
//     dedicated (all preds inside the loop) — LLVM 的第三项规范化保证，
//     LCSSA 的前置条件（LoopVerify L2 据此断言）。
//   - Updates phi nodes in header / exit blocks accordingly.
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
    bool insertBackedgeBlock(Loop *loop, Function *func);
    bool insertDedicatedExits(Loop *loop, Function *func);
};
