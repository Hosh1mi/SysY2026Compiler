#pragma once

// Collapse a loop-carried overwrite to the iteration that produces its live
// result.  The initial control value is clamped to the last executing value,
// while the original zero-iteration path remains intact.

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/functionTerminationAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class LastIterationElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LastIterationElimination"; }

private:
    bool runOnFunction(Function *function, BasicAliasAnalysis &aliasAnalysis,
                       FunctionTerminationAnalysis &terminationAnalysis);
    bool tryTransform(Loop &loop, Function *function,
                      BasicAliasAnalysis &aliasAnalysis,
                      FunctionTerminationAnalysis &terminationAnalysis);
};
