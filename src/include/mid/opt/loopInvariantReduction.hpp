#pragma once

#include "pass.hpp"

// Hoist a private, loop-invariant store region and extract a pure inner
// reduction from a modulo reduction in the enclosing loop.  This is kept
// separate from ScalarExpansion: the latter performs scalar expansion and
// loop interchange for reduction nests.
class LoopInvariantReduction : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopInvariantReduction"; }
};
