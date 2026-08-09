#pragma once
// LCSSA —— 构造 Loop-Closed SSA。
//
// 循环内定义在环外的每个 use 改经 exit 顶部的 live-out phi。
//
// 典型支持形式：
//   loop 内 %v 在 exit 后被使用 → %v.lcssa = phi [%v, ...]
//
// 需要 LoopSimplify 的 dedicated exits。成功后环外只通过 exit phi
// 引用环内值，便于 rotate / unroll / vectorize 等改写循环体。

#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

class LCSSA : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LCSSA"; }
    LoopForm requiredLoopForm() const override {
        return LoopForm::Simplified;
    }
    LoopForm establishedLoopForm() const override {
        return LoopForm::LCSSA;
    }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
    bool runOnLoop(Loop *loop, const DominatorTreeAnalysis &DT);
};
