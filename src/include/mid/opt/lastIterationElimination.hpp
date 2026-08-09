#pragma once
// LastIterationElimination —— 仅关心覆盖型 live-out 时钳到末次迭代。
//
// 将循环携带的覆盖写收束到产生最终 live 结果的那一轮。
//
// 典型支持形式：
//   每轮覆盖写同一 live-out，最终只依赖最后一次写入
//   → 将 IV/控制值钳到末次迭代，保留零次迭代路径
//
// 中间迭代若有其他可观察效应则不变换。

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/functionTerminationAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class LastIterationElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LastIterationElimination"; }

private:
    bool runOnFunction(Function *function, BasicAliasAnalysis &aliasAnalysis,
                       FunctionTerminationAnalysis &terminationAnalysis);
    bool tryTransform(Loop &loop, Function *function,
                      BasicAliasAnalysis &aliasAnalysis,
                      FunctionTerminationAnalysis &terminationAnalysis);
};
