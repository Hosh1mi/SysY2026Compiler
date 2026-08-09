#pragma once
// CorrelatedValuePropagation —— 沿控制边的局部常量与谓词传播。
//
// 基于 LazyValueInfo，在边与 use 点细化取值，折叠相关比较与分支。
//
// 典型支持形式：
//   if (x == 0) ... use(x) → use(0)
//   phi 在已知边上来自常量 → 边细化
//   select / icmp / zext 在 use 点可证常量或恒真/恒假
//   条件 br 的条件为常量 → 无条件跳转
//
// 侧重边敏感事实；全局稀疏常量传播由 SCCP 负责。

#include "pass.hpp"

class CorrelatedValuePropagation : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "CorrelatedValuePropagation"; }

private:
    bool runOnModule(Module *module, AnalysisManager &AM);
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
