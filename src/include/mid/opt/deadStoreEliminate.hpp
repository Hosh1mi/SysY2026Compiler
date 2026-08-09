#pragma once
// DeadStoreEliminate —— 删除被覆盖或冗余回写的死 store。
//
// 识别对同一地址的无用写：后续 MustAlias store 覆盖，或 load→同址
// store 的 writeback 且中间无修改。
//
// 典型支持形式：
//   *p = a; *p = b;              // 前者被覆盖
//   t = *p; ...; *p = t;         // 无中间修改的回写
//   Full 模式：跨基本块、由支配关系保证的冗余回写
//
// MayAlias 的 store 不删除。成功后去掉对后续可观察状态无贡献的写。

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
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
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
