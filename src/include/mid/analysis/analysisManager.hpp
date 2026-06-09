#pragma once

#include "basicAliasAnalysis.hpp"
#include "loopInfo.hpp"
#include "preservedAnalyses.hpp"
#include "scalarEvolution.hpp"

#include <map>
#include <memory>
#include <string>

class AnalysisManager {
public:
    BasicAliasAnalysis &getBasicAA(Module *module);
    LoopInfo &getLoopInfo(Function *func);
    ScalarEvolution &getScalarEvolution(Function *func);

    void invalidate(Module *module, const PreservedAnalyses &pa);
    void invalidateFunction(Function *func, const PreservedAnalyses &pa);
    void clear(Function *func);
    void clear();

private:
    struct FunctionCache {
        std::unique_ptr<LoopInfo> loopInfo;
        std::unique_ptr<ScalarEvolution> scalarEvolution;
    };

    void debug(const char *event, const char *analysis, const std::string &unit) const;
    void invalidateFunctionCache(FunctionCache &cache, const PreservedAnalyses &pa);

    Module *basicAAModule_ = nullptr;
    std::unique_ptr<BasicAliasAnalysis> basicAA_;
    std::map<Function *, FunctionCache> functionCaches_;
};
