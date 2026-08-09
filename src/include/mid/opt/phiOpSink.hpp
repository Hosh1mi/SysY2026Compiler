#pragma once
// PhiOpSink —— 将 phi 各边上相同的二元运算下沉到 phi 之后。
//
// 当各前驱对对应操作数做同构二元运算时，先汇合操作数再算一次。
//
// 典型支持形式：
//   A: t1 = x1 + c; B: t2 = x2 + c; p = phi(t1,t2)
//     → p = phi(x1,x2); t = p + c
//
// 要求各边运算同构且可安全下沉。成功后减少重复运算。

#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

class PhiOpSink : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "PhiOpSink"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
    bool trySinkPhi(PhiInst *phi, Function *func, LoopInfo &LI,
                    const DominatorTreeAnalysis &DT);
};
