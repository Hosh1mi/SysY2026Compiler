#pragma once

#include "costModel.hpp"
#include "dependenceAnalysis.hpp"
#include "loopAccessAnalysis.hpp"

struct LoopInterchangeCost {
    long before = -1;
    long after = -1;

    bool known() const { return before >= 0 && after >= 0; }
    bool profitable() const { return known() && after < before; }
};

struct ParallelSinkAnalysisResult {
    bool accepted = false;
    const char *reason = "not analyzed";
    Loop *cost_loop = nullptr;
    LoopAccessInfo access_info;
    LoopInterchangeCost cost;
};

class LoopInterchangeAnalysis {
public:
    LoopInterchangeAnalysis(DependenceAnalysis &DA,
                            LoopAccessAnalysis &LA,
                            CostModel &CM)
        : DA_(&DA), LA_(&LA), CM_(&CM) {}

    bool isInterchangeLegal(Loop *outer, Loop *inner,
                            const std::vector<Instruction *> &accesses) const;

    LoopInterchangeCost estimateCost(
        const std::vector<GetElementPtrInst *> &geps,
        PhiInst *beforeInnerIV,
        PhiInst *afterInnerIV) const;

    Loop *deepestCanonicalDescendant(Loop *loop) const;
    bool hasNonIVHeaderPhi(Loop *loop) const;

    ParallelSinkAnalysisResult analyzeParallelSink(Loop *loop) const;

private:
    DependenceAnalysis *DA_;
    LoopAccessAnalysis *LA_;
    CostModel *CM_;
};

