#pragma once

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

// LoopMemoryScalarPromotion:
// Promote a loop-carried memory cell to a scalar alloca mirror when the pointer
// is loop-invariant and every loop access to that memory is an exact load/store
// through the same SSA pointer. A following Mem2Reg turns the mirror into SSA.
class LoopMemoryScalarPromotion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopMemoryScalarPromotion"; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    bool tryPromote(Loop &loop, const BasicAliasAnalysis &BAA);
};
