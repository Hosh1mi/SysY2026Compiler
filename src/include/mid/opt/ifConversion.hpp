#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// IfConversion: 将内层循环中纯算术 if-body 转换为 select，消除条件分支。
//
// 目标模式（4块内层循环）：
//   H  → B or exit    (循环头，出口条件)
//   B  → T or L       (计算 + 条件分支)
//   T  → L            (if-body：纯算术/load，无 store/call/div)
//   L  → H            (latch：phi + store + 归纳变量更新)
//
// 变换后（3块）：
//   H  → B or exit
//   B  → L            (含 T 的指令 + select 替换 phi)
//   L  → H
//
// 收益：消除循环内条件分支（避免 A53 分支预测开销），并将循环压为 3 块，
// 使 LoopVectorize 可以向量化（vectorizer 拒绝 >3 块）。
class IfConversion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IfConversion"; }

private:
    bool runOnFunction(Function *func);
};
