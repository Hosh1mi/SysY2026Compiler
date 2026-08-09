#pragma once
// GVN —— 循环/内联之后的全局值编号 CSE。
//
// 在支配树上做值编号，消除冗余计算并合并等价 phi。
//
// 典型支持形式：
//   支配域内重复的纯表达式 → 复用
//   等价 phi 合并
//   纯 / readonly call 的 CSE
//   unroll / IV 变换后暴露的重复 GEP 与标量运算
//
// 有意不对 icmp/fcmp 做 CSE。早期支配域 CSE 由 EarlyCSE 负责。

#include "pass.hpp"
#include "../ir/ir.hpp"

class GVN : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "GVN"; }
};
