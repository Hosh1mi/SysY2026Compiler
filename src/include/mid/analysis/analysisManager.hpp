#pragma once

#include "basicAliasAnalysis.hpp"
#include "dominanceAnalysis.hpp"
#include "lazyValueInfo.hpp"
#include "loopInfo.hpp"
#include "preservedAnalyses.hpp"
#include "rangeAnalysis.hpp"
#include "scalarEvolution.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>

class AnalysisManager {
public:
    BasicAliasAnalysis &getBasicAA(Module *module);
    DominatorTreeAnalysis &getDominatorTree(Function *func);
    PostDominatorTreeAnalysis &getPostDominatorTree(Function *func);
    DominanceFrontierAnalysis &getDominanceFrontier(Function *func);
    LazyValueInfo &getLazyValueInfo(Function *func);
    LoopInfo &getLoopInfo(Function *func);
    RangeAnalysis &getRangeAnalysis(Function *func);
    ScalarEvolution &getScalarEvolution(Function *func);
    bool isRangeAnalysisActive(Function *func) const;
    void enterRangeAnalysis(Function *func);
    void leaveRangeAnalysis(Function *func);

    void invalidate(Module *module, const PreservedAnalyses &pa);
    void invalidateFunction(Function *func, const PreservedAnalyses &pa);
    void clearRangeAnalyses();
    void clear(Function *func);
    void clear();

private:
    struct FunctionCache {
        std::unique_ptr<DominatorTreeAnalysis> dominatorTree;
        std::unique_ptr<PostDominatorTreeAnalysis> postDominatorTree;
        std::unique_ptr<DominanceFrontierAnalysis> dominanceFrontier;
        std::unique_ptr<LazyValueInfo> lazyValueInfo;
        std::unique_ptr<LoopInfo> loopInfo;
        std::unique_ptr<RangeAnalysis> rangeAnalysis;
        std::unique_ptr<ScalarEvolution> scalarEvolution;
    };

    void debug(const char *event, const char *analysis, const std::string &unit) const;
    void invalidateFunctionCache(FunctionCache &cache, const PreservedAnalyses &pa);

    Module *basicAAModule_ = nullptr;
    std::unique_ptr<BasicAliasAnalysis> basicAA_;
    std::map<Function *, FunctionCache> functionCaches_;
    std::set<Function *> activeRangeAnalyses_;
};
