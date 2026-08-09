#pragma once
// LoopMemoryScalarPromotion —— 循环不变指针上的标量内存提升为 alloca 镜像。
//
// 当指针循环不变且环内对该单元仅经同一 SSA 指针精确 load/store 时，
// 用局部镜像承接，条件写回保持语义。
//
// 典型支持形式：
//   for (...) { t = *p; ...; *p = t'; }（p 不变）→ alloca 镜像
//
// 逃逸、别名不确定或访问形态不精确则不提升。随后可由 Mem2Reg 成 SSA。

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class LoopMemoryScalarPromotion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopMemoryScalarPromotion"; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &BAA,
                       AnalysisManager &AM);
    bool tryPromote(Loop &loop, const BasicAliasAnalysis &BAA,
                    const DominatorTreeAnalysis &DT);
};
