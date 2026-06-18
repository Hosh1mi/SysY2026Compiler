#include "../../include/mid/analysis/analysisManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static bool isAnalysisManagerDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_ANALYSIS_MANAGER") != nullptr;
    return enabled;
}

void AnalysisManager::debug(const char *event, const char *analysis,
                            const std::string &unit) const {
    if (!isAnalysisManagerDebugEnabled()) return;
    std::cerr << "[AnalysisManager] " << event
              << " analysis=" << analysis
              << " unit=" << unit << "\n";
}

BasicAliasAnalysis &AnalysisManager::getBasicAA(Module *module) {
    if (!basicAA_ || basicAAModule_ != module) {
        debug("miss", "BasicAA", "module");
        basicAA_ = std::make_unique<BasicAliasAnalysis>();
        basicAA_->analyze(module);
        basicAAModule_ = module;
    } else {
        debug("hit", "BasicAA", "module");
    }
    return *basicAA_;
}

LazyValueInfo &AnalysisManager::getLazyValueInfo(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.lazyValueInfo) {
        debug("miss", "LazyValueInfo", func ? func->name_ : "<null>");
        cache.lazyValueInfo = std::make_unique<LazyValueInfo>();
    } else {
        debug("hit", "LazyValueInfo", func ? func->name_ : "<null>");
    }
    cache.lazyValueInfo->analyze(func);
    return *cache.lazyValueInfo;
}

LoopInfo &AnalysisManager::getLoopInfo(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.loopInfo) {
        debug("miss", "LoopInfo", func ? func->name_ : "<null>");
        cache.loopInfo = std::make_unique<LoopInfo>();
        cache.loopInfo->analyze(func);
    } else {
        debug("hit", "LoopInfo", func ? func->name_ : "<null>");
    }
    return *cache.loopInfo;
}

RangeAnalysis &AnalysisManager::getRangeAnalysis(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.rangeAnalysis) {
        debug("miss", "RangeAnalysis", func ? func->name_ : "<null>");
        LoopInfo &LI = getLoopInfo(func);
        ScalarEvolution &SE = getScalarEvolution(func);
        cache.rangeAnalysis = std::make_unique<RangeAnalysis>(func, this, LI, SE);
    } else {
        debug("hit", "RangeAnalysis", func ? func->name_ : "<null>");
    }
    return *cache.rangeAnalysis;
}

ScalarEvolution &AnalysisManager::getScalarEvolution(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.scalarEvolution) {
        debug("miss", "ScalarEvolution", func ? func->name_ : "<null>");
        LoopInfo &LI = getLoopInfo(func);
        cache.scalarEvolution = std::make_unique<ScalarEvolution>(LI);
    } else {
        debug("hit", "ScalarEvolution", func ? func->name_ : "<null>");
    }
    return *cache.scalarEvolution;
}

bool AnalysisManager::isRangeAnalysisActive(Function *func) const {
    return activeRangeAnalyses_.count(func) != 0;
}

void AnalysisManager::enterRangeAnalysis(Function *func) {
    if (func) activeRangeAnalyses_.insert(func);
}

void AnalysisManager::leaveRangeAnalysis(Function *func) {
    if (func) activeRangeAnalyses_.erase(func);
}

void AnalysisManager::invalidateFunctionCache(FunctionCache &cache,
                                              const PreservedAnalyses &pa) {
    if (pa.preservesAll()) return;

    if (!pa.preservesLVI()) {
        cache.lazyValueInfo.reset();
    }

    if (!pa.preservesLoopInfo()) {
        cache.rangeAnalysis.reset();
        cache.scalarEvolution.reset();
        cache.loopInfo.reset();
        return;
    }

    if (!pa.preservesSCEV()) {
        cache.rangeAnalysis.reset();
        cache.scalarEvolution.reset();
    }
}

void AnalysisManager::invalidate(Module *module, const PreservedAnalyses &pa) {
    if (pa.preservesAll()) {
        debug("preserve", "all", "module");
        return;
    }

    if (!pa.preservesBasicAA()) {
        if (basicAA_ && basicAAModule_ == module)
            debug("invalidate", "BasicAA", "module");
        basicAA_.reset();
        basicAAModule_ = nullptr;
    }

    for (auto &entry : functionCaches_) {
        invalidateFunctionCache(entry.second, pa);
    }
}

void AnalysisManager::invalidateFunction(Function *func,
                                         const PreservedAnalyses &pa) {
    if (pa.preservesAll()) return;
    auto it = functionCaches_.find(func);
    if (it == functionCaches_.end()) return;

    if (!pa.preservesLoopInfo())
        debug("invalidate", "LoopInfo/SCEV", func ? func->name_ : "<null>");
    else if (!pa.preservesSCEV())
        debug("invalidate", "ScalarEvolution", func ? func->name_ : "<null>");

    invalidateFunctionCache(it->second, pa);
}

void AnalysisManager::clear(Function *func) {
    debug("clear", "function", func ? func->name_ : "<null>");
    functionCaches_.erase(func);
}

void AnalysisManager::clear() {
    debug("clear", "all", "module");
    basicAA_.reset();
    basicAAModule_ = nullptr;
    functionCaches_.clear();
}
