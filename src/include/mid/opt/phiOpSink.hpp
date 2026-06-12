#pragma once

#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

class PhiOpSink : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "PhiOpSink"; }

private:
    bool runOnFunction(Function *func);
    bool trySinkPhi(PhiInst *phi, Function *func, LoopInfo &LI);
};
