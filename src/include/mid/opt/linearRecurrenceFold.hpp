#pragma once

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

#include <cstdint>
#include <vector>

// LinearRecurrenceFold: 识别循环内耦合线性状态递推 x <- A x（模 2^32），
// 在环上将出口线性型 c^T x_n = c^T A^n x_0 闭式求值后删除原循环。
//
// 求值策略（由 A、c 推出，不认源码成语）：
//   1) 若存在 λ 使 c^T A = λ c^T，则发射 λ^n * (c^T x_0)
//   2) 否则发射矩阵幂：先算 y = A^n x_0，再 c^T y
class LinearRecurrenceFold : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LinearRecurrenceFold"; }

private:
    void runOnFunction(Function *func, AnalysisManager *AM);
    bool tryFold(Loop &loop, Module *module);
};
