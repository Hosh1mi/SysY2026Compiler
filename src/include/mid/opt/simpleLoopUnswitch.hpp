#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

#include <unordered_map>

// SimpleLoopUnswitch: 循环不变条件外提（plan 阶段 3.3.2，非平凡形式）。
// 找循环内"条件不变、两个目标都在循环内"的条件分支，整循环克隆为
// 真/假两个版本，preheader 改为按条件分派；两份拷贝中该分支折叠成
// 无条件跳转（死路径交给后续 CFGSimplify/DCE 清理）。
//
// 流水线位置：LCSSA 之后、LoopRotate 之前（LCSSA 维持窗口内）——
// 循环内定义的循环外使用必然经 exit phi，克隆后只需给 exit phi 补
// 克隆侧入边，无需重建出口值（违反此假设的循环防御性 bail）。
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
