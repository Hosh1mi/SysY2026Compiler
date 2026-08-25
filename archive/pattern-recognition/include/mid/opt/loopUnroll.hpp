#pragma once
// LoopUnroll —— 对结构规则的计数循环做 2×/4×/8× 展开。
//
// 复制循环体以降低分支开销并暴露 ILP，同时生成 remainder 循环。
//
// 典型支持形式：
//   for (i = 0; i < n; ++i) { body; }
//     → 主循环一次执行 N 份 body + remainder 循环
//   LoopRotate 后的 do-while 计数循环
//
// 体积 / 访存 / 寄存器压力过大，或不规则控制流（如复杂 side-exit）
// 时不展开。成功后保留处理 remainder 的标量循环。

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <unordered_map>

class LoopUnroll : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopUnroll"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *func, BasicAliasAnalysis &BAA);
    bool tryUnroll(Loop &loop, Function *func, Module *module,
                   BasicAliasAnalysis &BAA);
    bool tryUnrollStructured(Loop &loop, Function *func, Module *module);
    bool tryUnrollStatefulWhileCFGRegion(Loop &loop, Function *func,
                                          Module *module);
    bool tryUnrollCFGRegion(Loop &loop, Function *func, Module *module);
    bool tryUnrollDoWhile(Loop &loop, Function *func, Module *module);
    Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                           const std::unordered_map<Value *, Value *> &vmap);
};
