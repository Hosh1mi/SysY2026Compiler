#pragma once

#include "pass.hpp"

class CodeSink : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "CodeSink"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
