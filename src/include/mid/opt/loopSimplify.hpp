#pragma once
// LoopSimplify —— 规范化自然循环形态。
//
// 为下游循环 Pass 提供 dedicated preheader、单一回边路径与 dedicated exits。
//
// 典型支持形式：
//   无 preheader → 插入 dedicated preheader
//   多回边 → 合并为单一 backedge 块
//   出口有环外前驱 → 分裂为 dedicated exit
//
// 纯规范化，不改变计算结果。LCSSA 依赖本 Pass 的 dedicated exits。

#include "pass.hpp"

class LoopSimplify : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopSimplify"; }
    LoopForm establishedLoopForm() const override {
        return LoopForm::Simplified;
    }
};
