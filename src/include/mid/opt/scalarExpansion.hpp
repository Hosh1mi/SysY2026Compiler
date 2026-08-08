#pragma once
// ScalarExpansion:
//   Rewrites a scalar-reduction loop nest into private scratch storage. The
//   resulting clear, interchanged compute, and store-back regions remove the
//   scalar reduction dependence while preserving the original result.

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopAccessAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/reductionAnalysis.hpp"
#include "pass.hpp"

class ScalarExpansion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "ScalarExpansion"; }

private:
    void runOnFunction(Function *func);
    bool apply(const ScalarReductionNestInfo &info, Module *module);
    bool isLegalAndProfitable(const ScalarReductionNestInfo &info,
                              LoopInterchangeAnalysis &IA);

    int scratch_counter_ = 0;
    int block_counter_ = 0;
};
