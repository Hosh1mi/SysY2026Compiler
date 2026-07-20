#pragma once

#include "pass.hpp"

class AnalysisDump : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "AnalysisDump"; }
    bool convergenceRelevant() const override { return false; }
};

