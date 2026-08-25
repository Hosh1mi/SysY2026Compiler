#pragma once
// LoopModuloDelay —— 推迟 loop-carried srem 到出口一次性归约。
//
// 用 i64 累加独立贡献，避免每轮有符号取模的回环依赖。
//
// 典型支持形式：
//   acc = (acc + x_i) % M → i64 累加，exit 处一次 reduce
//
// 在 LoopRepFold 消费闭式之后、展开破坏模递推形态之前运行。无法证明
// 与原 srem 语义等价时不变换。

#include "pass.hpp"

class LoopModuloDelay : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopModuloDelay"; }
};
