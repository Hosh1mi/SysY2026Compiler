#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class InstCombine : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "InstCombine"; }

private:
    bool runOnFunction(Function *func, AnalysisManager *AM);
};
