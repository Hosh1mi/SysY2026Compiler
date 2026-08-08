#pragma once

#include "pass.hpp"

class AnalysisManager;
class BasicAliasAnalysis;

class DeadCodeEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "DeadCodeEliminate"; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    bool eliminateUnreachableFunctions(Module *module);
    bool eliminateUnusedGlobals(Module *module);
    bool eliminateDeadInstructions(Function *func,
                                   const BasicAliasAnalysis &BAA);
    bool eliminateTrivialPhis(Function *func);

    bool hasRequiredEffect(Instruction *inst,
                           const BasicAliasAnalysis &BAA) const;
};
