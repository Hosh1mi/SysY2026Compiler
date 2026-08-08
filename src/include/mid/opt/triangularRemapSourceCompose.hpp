#pragma once

// TriangularRemapSourceCompose: replace a fully proven sequence of ordered
// triangular copies with demand-driven source tracing at the original loads.
// The pass versions the loop region, preserves the original consumer IR, and
// keeps the original path whenever runtime bounds do not prove safe accesses.

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class TriangularRemapSourceCompose : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangularRemapSourceCompose"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *function, AnalysisManager &AM);
};
