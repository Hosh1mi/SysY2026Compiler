#pragma once
// ScalarExpansion —— 将标量 reduction 巢展开为私有 scratch 后交换。
//
// 用 clear / 交换后的 compute / store-back 去掉标量 reduction 依赖，
// 使内层可按更优遍历顺序执行。
//
// 典型支持形式：
//   for (p) {
//     s = init;
//     for (i) s += f(A[p][i], ...);
//     B[p] = s;
//   }
//   → 私有 scratch 清零 / 交换后的向量友好计算 / 写回
//
// 需要 reduction 形态可识别、内存合法且 interchange 代价模型盈利。
// 与 LoopInterchange 分工：本 Pass 处理需要标量展开的 reduction 巢；
// 无 loop-carried 依赖的纯并行层下沉由 LoopInterchange 负责。

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
