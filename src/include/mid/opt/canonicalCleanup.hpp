#pragma once

// CanonicalCleanup repeatedly applies the cheap canonicalization passes until
// the local IR reaches a fixed point or the iteration budget is exhausted.

#include "pass.hpp"

#include <string>

class CanonicalCleanup : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "CanonicalCleanup"; }
    bool convergenceRelevant() const override { return false; }
};
