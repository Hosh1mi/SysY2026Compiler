#pragma once
// SimpleLoopUnswitch —— 对循环不变条件做循环版本分裂。
//
// 克隆循环为真/假两版本，在 preheader 按不变条件分派。
//
// 典型支持形式：
//   for (...) if (inv) ... else ... →
//     if (inv) loop_true else loop_false
//
// 面向最内层、条件不变且两后继都在环内的分支。成功后环内该分支
// 变为无条件跳转。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

#include <unordered_map>

class SimpleLoopUnswitch : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SimpleLoopUnswitch"; }

private:
    bool runOnFunction(Function *func);
    bool tryUnswitch(Loop &loop, Function *func, int remainingGrowth,
                     int *clonedInsts);
    Instruction *cloneInst(Instruction *oldInst, BasicBlock *newBB,
                           std::unordered_map<Value *, Value *> &valMap);

    std::unordered_map<Function *, int> growthUsed_;
};
