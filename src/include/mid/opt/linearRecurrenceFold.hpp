#pragma once
// LinearRecurrenceFold —— 折叠小规模线性状态递推的出口闭式。
//
// 识别 x ← A x 的耦合线性系统，将出口线性型 c^T x_n 闭式求值后删循环。
//
// 典型支持形式：
//   小状态向量的线性更新，出口为状态的线性组合
//   可对角化为 λ^n 或经矩阵幂求值
//
// 仅处理可证明的小系统；非线性或含副作用的循环不折叠。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

#include <cstdint>
#include <vector>

class LinearRecurrenceFold : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LinearRecurrenceFold"; }

private:
    void runOnFunction(Function *func, AnalysisManager *AM);
    bool tryFold(Loop &loop, Module *module);
};
