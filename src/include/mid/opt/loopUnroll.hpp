#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <unordered_map>

// LoopUnroll：对 header+latch 两块结构的计数循环做 4×（高压力时 2×）展开。
// 循环结构统一来自 LoopInfo（plan 阶段 3.1）。
class LoopUnroll : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopUnroll"; }

private:
    void runOnFunction(Function *func);
    bool tryUnroll(Loop &loop, Function *func, Module *module);
    Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                           const std::unordered_map<Value *, Value *> &vmap);
};
