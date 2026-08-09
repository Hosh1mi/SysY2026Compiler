#pragma once
// VectorCombine recovers target-native vector operations from scalarized IR.
//
// It combines lane-local arithmetic, complete extract/insert rebuilds,
// and adjacent shuffle chains when the shared target cost model predicts a
// strict reduction.  It deliberately does not infer source-language vector
// mask or floating-point reassociation semantics.

#include "pass.hpp"

class VectorCombine : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "VectorCombine"; }

private:
    bool runOnFunction(Function *function);
};
