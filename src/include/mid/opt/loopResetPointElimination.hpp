#pragma once

// LoopResetPointElimination detects loop-carried integer memory recurrences
// whose old state is multiplied by a per-iteration factor.  When factor zero
// provably overwrites the complete state and the skipped prefix has no other
// effects, it starts the loop at the last dynamic reset point.

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class ArgumentAliasAnalysis;
class BasicAliasAnalysis;

class LoopResetPointElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopResetPointElimination"; }

private:
    bool runOnFunction(Function *func, Module *module,
                       BasicAliasAnalysis &basicAA,
                       ArgumentAliasAnalysis &argumentAA,
                       AnalysisManager &AM);
};
