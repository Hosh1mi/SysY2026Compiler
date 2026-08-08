#pragma once

#include "pass.hpp"
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/dominanceAnalysis.hpp"

enum class DeadStoreEliminateMode {
    Lite,
    Full,
};

class DeadStoreEliminate : public Pass {
public:
    explicit DeadStoreEliminate(
        DeadStoreEliminateMode mode = DeadStoreEliminateMode::Full)
        : mode_(mode) {}

    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return mode_ == DeadStoreEliminateMode::Lite
            ? "DeadStoreEliminateLite"
            : "DeadStoreEliminate";
    }

private:
    DeadStoreEliminateMode mode_;

    bool runOnFunction(Function *func, const BasicAliasAnalysis &AA,
                       const DominatorTreeAnalysis &DT);
    bool isLocallyOverwritten(StoreInst *store,
                              const BasicAliasAnalysis &AA);
    bool isLocallyRedundantWriteback(StoreInst *store,
                                     const BasicAliasAnalysis &AA);
    bool isRedundantStore(StoreInst *store, const BasicAliasAnalysis &AA,
                          const DominatorTreeAnalysis &DT);
};
