#pragma once
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <unordered_map>

// LoopUnroll：对计数循环做 4×（高压力时 2×）展开。
// 两种形态（plan 3.1.5）：
//   - while 形：header（phi+cmp+条件跳）+ latch（body+无条件回跳）两块；
//   - do-while 形（LoopRotate 之后的单块循环）：body+iv 更新+cmp（测更新
//     后的值）+条件回跳。展开为 guard → 主循环（N×body）→ 余数守卫 →
//     原循环（余数，可 0 次），exit 的 live-out 经新 phi 汇合。
// 循环结构统一来自 LoopInfo（plan 阶段 3.1）。
class LoopUnroll : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopUnroll"; }

private:
    void runOnFunction(Function *func, BasicAliasAnalysis &BAA);
    bool tryUnroll(Loop &loop, Function *func, Module *module,
                   BasicAliasAnalysis &BAA);
    bool tryUnrollStructured(Loop &loop, Function *func, Module *module);
    bool tryUnrollCFGRegion(Loop &loop, Function *func, Module *module);
    bool tryUnrollDoWhile(Loop &loop, Function *func, Module *module);
    Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                           const std::unordered_map<Value *, Value *> &vmap);
};
