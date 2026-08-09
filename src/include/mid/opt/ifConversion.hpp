#pragma once
// IfConversion —— 将内层循环 if-body 转为 select。
//
// 消除循环内条件分支，压平控制流，便于后续向量化等变换。
//
// 典型支持形式：
//   最内层 if (c) x = ...; 纯算术 body → select
//   store 地址菱形：选地址 + 单一 store
//
// 面向结构规则的内层循环；复杂副作用或无法证明安全的分支不转换。
// 一般 CFG 菱形转 select 由 CFGSimplify 负责。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

class IfConversion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IfConversion"; }

private:
    bool runOnFunction(Function *func);
};
