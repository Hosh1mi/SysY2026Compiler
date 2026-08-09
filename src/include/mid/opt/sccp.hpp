#pragma once
// SCCP —— 稀疏条件常量传播。
//
// 在可达边上传播常量，折叠常量运算并切断不可达边/块。
//
// 典型支持形式：
//   x = 3; y = x + 1 → y = 4
//   if (false) ... → 死边删除
//   常量条件下的 phi 边收缩
//
// 以整数为主；不做内存内容传播。边敏感的局部细化由
// CorrelatedValuePropagation 互补。

#include "pass.hpp"
#include "../ir/ir.hpp"

class SCCP : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SCCP"; }
private:
    bool runOnFunction(Function *func);
};
