#pragma once

#include "pass.hpp"

class IndVarSimplify : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IndVarSimplify"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
