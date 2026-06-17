#pragma once

#include "pass.hpp"

class LinearBlockMerge : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LinearBlockMerge"; }
    bool convergenceRelevant() const override { return false; }

private:
    bool runOnFunction(Function *func);
};
