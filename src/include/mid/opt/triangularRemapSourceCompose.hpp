#pragma once
// TriangularRemapSourceCompose —— 用按需溯源替换已证明的有序三角拷贝链。
//
// 在 load 侧按需追踪真正数据源，去掉中间三角重映射拷贝。
//
// 典型支持形式：
//   多段有序三角拷贝 A→B→C 后再从 C load
//     → 在消费 load 处按需溯源到真正 source
//
// 对循环区域做 versioning；运行时界无法证明访问安全时走原路径。

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class TriangularRemapSourceCompose : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangularRemapSourceCompose"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }

private:
    bool runOnFunction(Function *function, AnalysisManager &AM);
};
