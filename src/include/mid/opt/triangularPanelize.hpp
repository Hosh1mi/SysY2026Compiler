#pragma once
// TriangularPanelize —— 将已证明的三角标量递推加宽为多 lane panel。
//
// 利用相邻输出共享的公共前缀，加宽前向三角 recurrence 的计算。
//
// 典型支持形式：
//   前向三角标量递推：out[k] 依赖 out[k] 的前缀累加
//   相邻输出共享公共前缀 → 加宽为 multi-lane panel
//
// 合法性来自完整标量 reduction 与仿射地址形态；无法匹配的巢保持不变。

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class TriangularPanelize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangularPanelize"; }

private:
    bool runOnFunction(Function *function);
};
