#pragma once
// LoopResetPointElimination —— 从最后一次乘性零复位开始内存递推。
//
// 当 loop-carried 整型状态被每轮因子缩放、且因子为 0 可完整覆盖旧状态时，
// 跳过无效果前缀，从最后复位点起算。
//
// 典型支持形式：
//   s = (s * f_i) + ...；某次 f_k == 0 清空状态
//     → 从最后一次清零之后开始
//
// 前缀若有其他副作用或复位无法证明完整覆盖则不变换。

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class ArgumentAliasAnalysis;
class BasicAliasAnalysis;

class LoopResetPointElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopResetPointElimination"; }

private:
    bool runOnFunction(Function *func, Module *module,
                       BasicAliasAnalysis &basicAA,
                       ArgumentAliasAnalysis &argumentAA,
                       AnalysisManager &AM);
};
