#pragma once
// LateValueCleanup —— 单前驱块中清理前驱已算过的纯运算。
//
// 在简单控制结构里删除与前驱重复的纯计算。
//
// 典型支持形式：
//   pred: t = a + b; br B
//   B(单前驱): u = a + b → 复用 t
//   同理的纯 mul / GEP / zext 重复计算
//
// 仅处理单前驱块中的纯运算；更广的值编号由 GVN / EarlyCSE 覆盖。

#include "pass.hpp"

class LateValueCleanup : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LateValueCleanup"; }
};
