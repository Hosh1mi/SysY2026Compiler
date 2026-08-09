#pragma once
// LoopDeletion —— 删除无副作用且无 live-out 的可计数循环。
//
// 证明循环不影响可观察状态后整体删除；也可打断恰好一次迭代的回边。
//
// 典型支持形式：
//   纯计数、无 store/call、无环外 live-out 的循环 → 直接跳 exit
//   恰一次迭代的回边 → 打断成直线
//
// 无法证明终止或存在副作用/live-out 则保留。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

class LoopDeletion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopDeletion"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *func);
    bool tryDelete(Loop &loop, Function *func);
    bool tryBreakSingleIterationBackedge(Loop &loop, Function *func);
};
