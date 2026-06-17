#pragma once

#include "pass.hpp"

class DeadCodeDelete : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "DeadCodeDelete"; }
    bool convergenceRelevant() const override { return false; }
private:
    bool runOnModule(Module *module);
    bool runOnFunction(Function *func);
    bool removeUnreachable(Function *func);

    // 激进死代码消除（标记-清扫）
    bool aggressiveDCE(Function *func);

    // 控制流简化（合并连续无条件跳转块）
    bool simplifyCFG(Function *func);

    // 辅助：检查指令是否具有副作用（不能被消除）
    bool isCriticalInstruction(Instruction *inst);

    // 辅助：从关键指令开始标记所有活跃指令
    void markLiveInstructions(Function *func, std::set<Instruction *> &live);

    // 原始的简单死代码删除（不再主用，可保留作参考）
    bool removeDeadInstructions(Function *func);

    void replacePhiUsesOfBlock(BasicBlock *old_bb, BasicBlock *new_bb);

    // 消除平凡 phi：所有非自引用操作数指向同一个值
    bool eliminateTrivialPhis(Function *func);
};
