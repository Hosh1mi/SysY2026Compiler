#pragma once

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// LoopIRCE: 简化版迭代域裁剪。
// 识别形如：
//   while (j < bound) {
//     if (outer < j) { j = j + 1; continue; }
//     work(j);
//     j = j + 1;
//   }
// 的内层循环，把上界收紧为 min(bound, outer + 1)。
// 第一版只处理 +1 递增 IV、单一 guard、continue 直回回边的紧形态。
class LoopIRCE : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopIRCE"; }

private:
    bool runOnFunction(Function *func);
    bool tryTightenLoop(Loop &loop, Module *module);
};
