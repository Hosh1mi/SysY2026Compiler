#pragma once
// InstCombine —— 局部代数化简与强度削弱。
//
// 对单条/短串指令做等价改写，暴露更简单的运算。
//
// 典型支持形式：
//   x * 8 → x << 3
//   x + 0 / x * 1 → x
//   select c, a, a → a
//   简单 icmp / 位运算恒等式
//
// 侧重局部模式；加法/乘法树的全局重结合由 Reassociate 负责。

#include "pass.hpp"
#include "../ir/ir.hpp"

class InstCombine : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "InstCombine"; }

private:
    bool runOnFunction(Function *func, AnalysisManager *AM);
};
