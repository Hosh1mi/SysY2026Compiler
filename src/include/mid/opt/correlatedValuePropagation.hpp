#pragma once

#include "pass.hpp"

class CorrelatedValuePropagation : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "CorrelatedValuePropagation"; }

private:
    bool runOnModule(Module *module, AnalysisManager &AM);
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
