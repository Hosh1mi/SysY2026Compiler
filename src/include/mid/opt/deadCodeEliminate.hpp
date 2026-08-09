#pragma once
// DeadCodeEliminate —— 删除无副作用的死代码与无用符号。
//
// 清理不可达/不可观察的指令、平凡 phi、未使用函数与全局变量。
//
// 典型支持形式：
//   unused:  a = b + c;          // a 无后续 use
//   phi:     x = phi(v, v) → v
//   func:    static void f() { ... } 且无调用点
//   global:  从未 load/store 的全局
//
// 保留 store / br / ret 以及可能有副作用的 call；仅 BAA 判定为纯的 call
// 可删。不负责覆盖写消除（由 DeadStoreEliminate 处理）。

#include "pass.hpp"

class AnalysisManager;
class BasicAliasAnalysis;

class DeadCodeEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    PassRunResult runPass(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "DeadCodeEliminate"; }

private:
    bool runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    bool eliminateUnreachableFunctions(Module *module);
    bool eliminateUnusedGlobals(Module *module);
    bool eliminateDeadInstructions(Function *func,
                                   const BasicAliasAnalysis &BAA);
    bool eliminateTrivialPhis(Function *func);

    bool hasRequiredEffect(Instruction *inst,
                           const BasicAliasAnalysis &BAA) const;
};
