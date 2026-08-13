#pragma once
// IndVarSimplify —— 规范化并化简归纳变量。
//
// 整理 IV 形态、折叠合同 IV，并在可证时简化范围比较与 trip。
//
// 典型支持形式：
//   常量 trip → 规范 0/+1 IV
//   多个合同 IV → 合并
//   可证的范围 icmp 折叠
//
// 需要 LCSSA。地址计算的指针步进削弱由 IndVarStrengthReduce 负责。

#include "pass.hpp"

class IndVarSimplify : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IndVarSimplify"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }
    LoopForm establishedLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
