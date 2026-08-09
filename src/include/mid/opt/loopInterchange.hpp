#pragma once
// LoopInterchange —— 将完全并行的循环下沉到最内层。
//
// 按 dependence 把无 loop-carried 依赖的层沉到内侧，以改善最内层
// 访存 stride；不完美嵌套时可伴随 distribution。
//
// 典型支持形式：
//   for (k) for (i) for (j) A[i][j] += ...;
//     若 k 对全部访存完全并行且作最内层更优 → 把 k 沉到最内
//   不完美嵌套 → 先分配直线段 / 子循环，再分别下沉
//
// 需要规范 induction variable，且代价模型证明更友好的最内 stride。
// 标量 reduction 巢的展开 + 交换由 ScalarExpansion 负责；本 Pass 只做
// 无依赖的纯置换 / 分配。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopAccessAnalysis.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class ArgumentAliasAnalysis;

class LoopInterchange : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopInterchange"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);

    const ArgumentAliasAnalysis *argAA_ = nullptr;  // 过程间参数别名 oracle
};
