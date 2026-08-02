#pragma once

// TriangularPanelize widens the common prefix shared by adjacent outputs of a
// forward triangular recurrence.  Legality is established from the complete
// scalar reduction and affine address shape; unmatched nests are unchanged.

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class TriangularPanelize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangularPanelize"; }

private:
    bool runOnFunction(Function *function);
};
