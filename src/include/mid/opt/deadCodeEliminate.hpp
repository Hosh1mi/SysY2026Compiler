#pragma once

#include "pass.hpp"

class AnalysisManager;

class DeadCodeEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "DeadCodeEliminate"; }
    bool convergenceRelevant() const override { return false; }

private:
    bool runOnFunction(Function *func);
    bool eliminateUnreachableFunctions(Module *module);
    bool eliminateUnusedGlobals(Module *module);
    bool eliminateDeadInstructions(Function *func);
    bool eliminateTrivialPhis(Function *func);

    bool hasRequiredEffect(Instruction *inst) const;
};
