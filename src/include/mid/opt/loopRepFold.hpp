#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>

// LoopRepFold: 循环重复折叠
// 识别并消除"纯重复计数循环"：while (r < R) { total += f(data); r++; }
// 其中 f(data) 不依赖 r 且不修改 data，变换为: total += f(data) * R（单次执行+乘法）
// 主要目标：many_mat_cal 中 157 亿次迭代 → T² 次计算 + 1 次乘法
// 循环结构统一来自 LoopInfo（plan 阶段 3.1）。
class LoopRepFold : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopRepFold"; }

private:
    void runOnFunction(Function *func);
    bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks);
    bool isCountingIV(PhiInst *phi, const Loop &loop, BasicBlock *latch);
    bool tryFold(Loop &loop, Module *module);
};
