#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// SplitGEP — strength-reduce 2D+ array row addresses.
//
// A multi-dimensional access like `A[i][j]` lowers to a single GEP whose
// outer-dimension multiply (`i * rowStride`) is invisible to the mid-end: it
// only materializes when the backend expands the GEP, so it is recomputed on
// every inner-loop iteration and cannot be CSE'd across accesses to the same
// row. This pass peels the loop-invariant index prefix into a standalone GEP
// hoisted to the loop preheader (computed once per outer iteration) and
// rewrites the access to index off that hoisted row base.
//
// Runs after ParallelizeLoops / LoopVectorize so those passes still see the
// original flat GEP shape their pattern matching depends on.
class SplitGEP : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SplitGEP"; }

private:
    bool runOnLoop(Loop *loop, LoopInfo &LI,
                   const DominatorTreeAnalysis &DT);
};
