#pragma once

#include "pass.hpp"

class CorrelatedValuePropagation : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    bool convergenceRelevant() const override { return false; }
    std::string name() const override { return "CorrelatedValuePropagation"; }

private:
    bool runOnModule(Module *module);
    bool runOnFunction(Function *func);
};
