#pragma once
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../analysis/scalarEvolution.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

// LICM：循环不变代码外提。循环结构统一来自 LoopInfo（plan 阶段 3.1），
// 不再维护手写的回边检测/preheader 推导。
class LICM : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LICM"; }

private:
    void runOnFunction(Function *func, AnalysisManager &AM);
    void eliminateSinglePredPhis(Function *func);
    bool eliminateTrivialHeaderPhis(const Loop &loop);

    bool isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                     const std::set<Instruction*>& toHoist, const Loop *loop = nullptr,
                     ScalarEvolution *SE = nullptr);
    bool isSafeToHoist(Instruction *inst, const Loop &loop,
                       const BasicAliasAnalysis &BAA);
    bool runOnLoop(const Loop &loop, ScalarEvolution *SE,
                   const BasicAliasAnalysis *BAA);
};
