#pragma once
// CodeSink —— 将纯运算下沉到各 use 的最近公共支配者。
//
// 推迟纯计算到更靠近使用点，缩短热路径上的活跃区间。
//
// 典型支持形式：
//   t = a + b; 若 use 只在深层分支 → 下沉到各 use 的 NCD
//   纯 GEP / 位运算同样可下沉
//
// 不下沉到会加深循环嵌套的位置；有副作用的指令不动。
// 成功后定义点更靠近 use。

#include "pass.hpp"

class CodeSink : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "CodeSink"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
