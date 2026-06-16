#pragma once

#include "pass.hpp"
#include "../analysis/preservedAnalyses.hpp"

class AnalysisManager;
class BasicBlock;
class Function;
class Module;

class TriangularLoopInterchange : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangularLoopInterchange"; }

private:
    bool runOnFunction(Function *func);
};
