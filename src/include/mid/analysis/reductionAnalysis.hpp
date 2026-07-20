#pragma once

#include "affineAnalysis.hpp"
#include "loopInfo.hpp"

#include <vector>

struct ScalarReductionInfo {
    PhiInst           *sum_phi = nullptr;
    Value             *sum_init = nullptr;
    Value             *sum_latch = nullptr;
    StoreInst         *store_inst = nullptr;
    GetElementPtrInst *gep_store = nullptr;
    Value             *base_store = nullptr;
    int                inner_dim = 0;
};

struct ScalarReductionNestInfo {
    Loop                            *inner_loop = nullptr;
    Loop                            *parent_loop = nullptr;
    std::vector<GetElementPtrInst *> body_geps;
    std::vector<ScalarReductionInfo> reductions;
    Value                           *inner_bound = nullptr;
    Value                           *parent_bound = nullptr;
};

class ReductionAnalysis {
public:
    explicit ReductionAnalysis(AffineAnalysis &AA) : AA_(&AA) {}

    bool detectScalarExpandableNest(Loop *inner, ScalarReductionNestInfo &out);
    bool isScalarExpansionMemoryLegal(const ScalarReductionNestInfo &info) const;

private:
    AffineAnalysis *AA_;
};
