#pragma once
// InductiveRangeCheckElimination —— 用单调 range guard 收紧 trip 域。
//
// 将循环内对 IV 的范围检查推导进 trip bound，去掉冗余 guard。
//
// 典型支持形式：
//   for (i = 0; i < N; ++i) if (i < U) body
//     → 上界收紧为 min(N, U)
//   递减 IV 对称地抬高下界
//
// 需要单 IV、可分析的单调/仿射 guard。成功后迭代域更紧，guard 可消除。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

class inductiveRangeCheckElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "inductiveRangeCheckElimination"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
    bool tryTightenLoop(Loop &loop, Module *module, const LoopInfo &LI,
                        const DominatorTreeAnalysis &DT);
};
