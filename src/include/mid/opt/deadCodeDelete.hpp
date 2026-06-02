#pragma once

#include "pass.hpp"

class DeadCodeDelete : public Pass {
public:
    void execute(Module *module) override;
private:
    // 删除函数内不可达的基本块
    bool removeDeadBlocks(Function *func);

    // 激进死代码消除（标记-清扫）
    bool aggressiveDCE(Function *func);

    // 控制流简化（合并连续无条件跳转块）
    bool simplifyCFG(Function *func);

    // 辅助：检查指令是否具有副作用（不能被消除）
    bool isCriticalInstruction(Instruction *inst);

    // 辅助：从关键指令开始标记所有活跃指令
    void markLiveInstructions(Function *func, std::set<Instruction *> &live);

    // 辅助：删除一个基本块及其内部所有指令，并更新使用链
    void deleteBasicBlock(BasicBlock *bb);

    // 辅助：更新所有基本块中的 Phi 指令，移除指向指定块的输入
    void updatePhiAfterRemoveBlock(BasicBlock *removed);

    // 原始的简单死代码删除（不再主用，可保留作参考）
    bool removeDeadInstructions(Function *func);

    void replacePhiUsesOfBlock(BasicBlock *old_bb, BasicBlock *new_bb);

    // 消除平凡 phi：所有非自引用操作数指向同一个值
    bool eliminateTrivialPhis(Function *func);
};
