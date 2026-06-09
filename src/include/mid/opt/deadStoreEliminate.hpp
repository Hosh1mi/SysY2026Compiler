#pragma once

#include "pass.hpp"
#include "../analysis/basicAliasAnalysis.hpp"

class DeadStoreEliminate : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "DeadStoreEliminate"; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &AA);
    bool isRedundantStore(StoreInst *store, const BasicAliasAnalysis &AA);
};
