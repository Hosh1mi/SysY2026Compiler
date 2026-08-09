#pragma once
// LoopPeel —— 剥下规范 2-BB 循环的首轮迭代。
//
// 将第一轮提到循环外，保留原有 side exit 语义。
//
// 典型支持形式：
//   规范 2-BB 循环剥首轮：先执行 iteration 0，再进入主循环
//   latch 上存在 side-exit 时仍保留该退出边
//
// 与 LoopUnroll 不同，本 Pass 保留 side exit，不做多倍展开。

#include "pass.hpp"

#include <string>

class LoopPeel : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopPeel"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }
};
