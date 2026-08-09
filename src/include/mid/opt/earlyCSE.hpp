#pragma once
// EarlyCSE —— 支配树作用域内的公共子表达式消除与简单内存转发。
//
// 在支配关系下消除重复计算，并做局部 store→load 转发。
//
// 典型支持形式：
//   y = a + b; z = a + b → 复用 y
//   *p = v; t = *p → t = v
//   纯 call 的重复结果复用
//
// 不做跨块 phi 合并（更全局的值编号由后续 GVN 负责）。

#include "pass.hpp"
#include "../ir/ir.hpp"

class EarlyCSE : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "EarlyCSE"; }
};
