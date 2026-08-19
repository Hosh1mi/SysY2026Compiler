// AnalysisManager 统一创建、缓存和失效中端分析。各 get 接口按需建立依赖，例如 LoopInfo
// 依赖支配树，RangeAnalysis 又依赖 LoopInfo 和 SCEV；优化 pass 修改 IR 后通过
// PreservedAnalyses 声明仍然有效的结果，管理器据此清理其余缓存。
#include "../../include/mid/analysis/analysisManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

// isAnalysisManagerDebugEnabled：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
static bool isAnalysisManagerDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_ANALYSIS_MANAGER") != nullptr;
    return enabled;
}

// debug：读取调试开关或输出分析中间结果，不参与正常语义判断。
void AnalysisManager::debug(const char *event, const char *analysis,
                            const std::string &unit) const {
    if (!isAnalysisManagerDebugEnabled()) return;
    std::cerr << "[AnalysisManager] " << event
              << " analysis=" << analysis
              << " unit=" << unit << "\n";
}

// getBasicAA：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
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

// getDominatorTree：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
DominatorTreeAnalysis &AnalysisManager::getDominatorTree(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.dominatorTree) {
        debug("miss", "DominatorTree", func ? func->name_ : "<null>");
        cache.dominatorTree = std::make_unique<DominatorTreeAnalysis>();
        cache.dominatorTree->analyze(func);
    } else {
        debug("hit", "DominatorTree", func ? func->name_ : "<null>");
    }
    return *cache.dominatorTree;
}

PostDominatorTreeAnalysis &
// getPostDominatorTree：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
AnalysisManager::getPostDominatorTree(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.postDominatorTree) {
        debug("miss", "PostDominatorTree", func ? func->name_ : "<null>");
        cache.postDominatorTree = std::make_unique<PostDominatorTreeAnalysis>();
        cache.postDominatorTree->analyze(func);
    } else {
        debug("hit", "PostDominatorTree", func ? func->name_ : "<null>");
    }
    return *cache.postDominatorTree;
}

DominanceFrontierAnalysis &
// getDominanceFrontier：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
AnalysisManager::getDominanceFrontier(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.dominanceFrontier) {
        debug("miss", "DominanceFrontier", func ? func->name_ : "<null>");
        cache.dominanceFrontier = std::make_unique<DominanceFrontierAnalysis>();
        cache.dominanceFrontier->analyze(func, getDominatorTree(func));
    } else {
        debug("hit", "DominanceFrontier", func ? func->name_ : "<null>");
    }
    return *cache.dominanceFrontier;
}

// getLazyValueInfo：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
LazyValueInfo &AnalysisManager::getLazyValueInfo(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.lazyValueInfo) {
        debug("miss", "LazyValueInfo", func ? func->name_ : "<null>");
        cache.lazyValueInfo = std::make_unique<LazyValueInfo>();
    } else {
        debug("hit", "LazyValueInfo", func ? func->name_ : "<null>");
    }
    cache.lazyValueInfo->analyze(func, &getLoopInfo(func),
                                 &getDominatorTree(func));
    return *cache.lazyValueInfo;
}

// getLoopInfo：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
LoopInfo &AnalysisManager::getLoopInfo(Function *func) {
    auto &cache = functionCaches_[func];
    if (!cache.loopInfo) {
        debug("miss", "LoopInfo", func ? func->name_ : "<null>");
        cache.loopInfo = std::make_unique<LoopInfo>();
        cache.loopInfo->analyze(func, getDominatorTree(func));
    } else {
        debug("hit", "LoopInfo", func ? func->name_ : "<null>");
    }
    return *cache.loopInfo;
}

// getRangeAnalysis：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
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

// getScalarEvolution：从 IR 和已有分析结果取得目标信息；缺少可靠结论时返回空值或保守结果。
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

// isRangeAnalysisActive：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool AnalysisManager::isRangeAnalysisActive(Function *func) const {
    return activeRangeAnalyses_.count(func) != 0;
}

// enterRangeAnalysis：封装该局部计算，为上层分析或 IR 构造返回所需结果。
void AnalysisManager::enterRangeAnalysis(Function *func) {
    if (func) activeRangeAnalyses_.insert(func);
}

// leaveRangeAnalysis：封装该局部计算，为上层分析或 IR 构造返回所需结果。
void AnalysisManager::leaveRangeAnalysis(Function *func) {
    if (func) activeRangeAnalyses_.erase(func);
}

// invalidateFunctionCache：封装该局部计算，为上层分析或 IR 构造返回所需结果。
void AnalysisManager::invalidateFunctionCache(FunctionCache &cache,
                                              const PreservedAnalyses &pa) {
    if (pa.preservesAll()) return;

    if (!pa.preservesPostDominatorTree())
        cache.postDominatorTree.reset();

    if (!pa.preservesDominatorTree()) {
        cache.dominanceFrontier.reset();
        cache.lazyValueInfo.reset();
        cache.rangeAnalysis.reset();
        cache.scalarEvolution.reset();
        cache.loopInfo.reset();
        cache.dominatorTree.reset();
        return;
    }

    if (!pa.preservesDominanceFrontier())
        cache.dominanceFrontier.reset();

    if (!pa.preservesLVI()) {
        cache.lazyValueInfo.reset();
    }

    if (!pa.preservesLoopInfo()) {
        cache.lazyValueInfo.reset();
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

// invalidate：封装该局部计算，为上层分析或 IR 构造返回所需结果。
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

// invalidateFunction：封装该局部计算，为上层分析或 IR 构造返回所需结果。
void AnalysisManager::invalidateFunction(Function *func,
                                         const PreservedAnalyses &pa) {
    if (pa.preservesAll()) return;
    auto it = functionCaches_.find(func);
    if (it == functionCaches_.end()) return;

    if (!pa.preservesDominatorTree())
        debug("invalidate", "DominatorTree/dependents",
              func ? func->name_ : "<null>");
    else if (!pa.preservesPostDominatorTree())
        debug("invalidate", "PostDominatorTree",
              func ? func->name_ : "<null>");
    else if (!pa.preservesDominanceFrontier())
        debug("invalidate", "DominanceFrontier",
              func ? func->name_ : "<null>");
    else if (!pa.preservesLoopInfo())
        debug("invalidate", "LoopInfo/SCEV", func ? func->name_ : "<null>");
    else if (!pa.preservesSCEV())
        debug("invalidate", "ScalarEvolution", func ? func->name_ : "<null>");

    invalidateFunctionCache(it->second, pa);
}

// clearRangeAnalyses：清除缓存和访问标记，保证 IR 改写后的查询重新计算。
void AnalysisManager::clearRangeAnalyses() {
    debug("clear", "RangeAnalysis", "all functions");
    activeRangeAnalyses_.clear();
    for (auto &entry : functionCaches_)
        entry.second.rangeAnalysis.reset();
}

// clear：清除缓存和访问标记，保证 IR 改写后的查询重新计算。
void AnalysisManager::clear(Function *func) {
    debug("clear", "function", func ? func->name_ : "<null>");
    functionCaches_.erase(func);
}

// clear：清除缓存和访问标记，保证 IR 改写后的查询重新计算。
void AnalysisManager::clear() {
    debug("clear", "all", "module");
    basicAA_.reset();
    basicAAModule_ = nullptr;
    functionCaches_.clear();
}
