#pragma once
// LoopInvariantReduction —— 外提私有不变填充与纯内层 reduction。
//
// 从外层模回绕中抽出与外层迭代无关的私有填充及纯内层归约。
//
// 典型支持形式：
//   外层 modulo wrap 内对私有区做不变填充
//   随后纯内层 reduction 与外层迭代无关 → 一并外提
//
// 与 ScalarExpansion 分离：后者做标量展开与 interchange，本 Pass 只做
// 可外提的不变填充/归约抽取。

#include "pass.hpp"

class LoopInvariantReduction : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopInvariantReduction"; }
};
