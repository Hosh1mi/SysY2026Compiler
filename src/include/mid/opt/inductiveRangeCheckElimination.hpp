#pragma once

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// inductiveRangeCheckElimination: 简化版迭代域裁剪。
// 识别单 IV、单 guard、单 latch 的紧形态循环，把显式 guard 推导成更紧的 trip
// bound：
//   - 递增 IV：tightenedUpper = min(origUpper, guardUpper)
//   - 递减 IV：tightenedLower = max(origLower, guardLower)
// 支持：
//   - 非零初值
//   - 步长 +1 / -1
//   - invariant、invariant +/- const 形式的 affine guard
//   - header 直接跳 latch 的 skip-path，或显式 continue 块
//   - rotated +1 loop 中、body/header 内的单调 guard 链区间裁剪
class inductiveRangeCheckElimination : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "inductiveRangeCheckElimination"; }

private:
    bool runOnFunction(Function *func);
    bool tryTightenLoop(Loop &loop, Module *module);
};
