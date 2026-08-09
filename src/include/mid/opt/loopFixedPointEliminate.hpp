#pragma once
// LoopFixedPointEliminate —— 非控制状态到达不动点时提前退出。
//
// 为可计数循环增加 early-exit：当 loop-carried 非控制状态本轮不变时，
// 余下迭代冗余。
//
// 典型支持形式：
//   状态 s 经迭代更新，若 s' == s 则可提前结束
//   计数 IV 仅服务于 trip guard
//
// 含 call、存取别名无法证明、或控制状态也参与递推时不变换。

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

class LoopFixedPointEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return "LoopFixedPointEliminate";
    }

private:
    bool runOnFunction(Function *func, BasicAliasAnalysis &AA);
    bool tryTransform(Loop &loop, Function *func, BasicAliasAnalysis &AA);
};
