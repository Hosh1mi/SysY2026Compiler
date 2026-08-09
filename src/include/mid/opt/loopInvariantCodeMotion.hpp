#pragma once
// LICM —— 循环不变代码外提。
//
// 将循环内不变且安全的运算/加载/纯调用提到 preheader。
//
// 典型支持形式：
//   for (...) { t = a + b; ... }（a,b 不变）→ preheader 计算 t
//   不变地址上的安全 load
//   纯 call 且参数不变
//
// 有副作用或别名无法证明安全的指令不外提。需要简化后的循环形态。

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

class LICM : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LICM"; }
private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
    bool eliminateTrivialHeaderPhis(const Loop &loop);

    bool isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                     const std::set<Instruction*>& toHoist);
    bool isSafeToHoist(Instruction *inst, const Loop &loop,
                       const BasicAliasAnalysis &BAA, const LoopInfo &LI);
    bool runOnLoop(const Loop &loop, const BasicAliasAnalysis *BAA,
                   const LoopInfo &LI);
};
