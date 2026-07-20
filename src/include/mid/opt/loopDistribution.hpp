#pragma once
// LoopDistribution:
//   Lowers the scalar-expansion reduction pattern into three ordered regions:
//   clear scratch, original compute loop, and store-back. The compute body is
//   kept in place; reduction phi uses are rewritten to load/store the local
//   scratch slot indexed by the parent IV.

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopAccessAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/reductionAnalysis.hpp"
#include "pass.hpp"

class LoopDistribution : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopDistribution"; }

private:
    void runOnFunction(Function *func);
    bool apply(const ScalarReductionNestInfo &info, Module *module);
    bool isLegalAndProfitable(const ScalarReductionNestInfo &info,
                              LoopInterchangeAnalysis &IA);

    int scratch_counter_ = 0;
    int block_counter_ = 0;
};

