#pragma once

#include "pass.hpp"
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/dominanceAnalysis.hpp"

class DeadStoreEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "DeadStoreEliminate"; }
    bool convergenceRelevant() const override { return false; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &AA,
                       const DominatorTreeAnalysis &DT);
    bool isRedundantStore(StoreInst *store, const BasicAliasAnalysis &AA,
                          const DominatorTreeAnalysis &DT);
};
