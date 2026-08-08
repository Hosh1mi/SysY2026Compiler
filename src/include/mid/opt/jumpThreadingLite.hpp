#pragma once

#include "pass.hpp"

class JumpThreadingLite : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "JumpThreadingLite"; }

private:
    bool runOnModule(Module *module);
    bool runOnFunction(Function *func, AnalysisManager *AM);
};
