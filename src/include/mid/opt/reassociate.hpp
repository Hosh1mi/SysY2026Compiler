#pragma once
// Reassociate —— 重结合整数加减乘树并提取公因子。
//
// 整理结合律下的运算树，合并同类项、暴露因式分解机会。
//
// 典型支持形式：
//   (a + b) + c 重排以利于 CSE / 常量折叠
//   A*B + A*C → A*(B+C)
//   同类项合并
//
// 仅整数加减乘树。局部单指令化简由 InstCombine 负责。

#include "pass.hpp"
#include "../ir/ir.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Reassociate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "Reassociate"; }

private:
    void runOnFunction(Function *func);
    bool changed_ = false;

    struct ValueEntry {
        Value *operand;
        int rank;
    };

    // Rank computation (RPO-based)
    std::unordered_map<Value*, int> valueRank_;
    std::unordered_map<BasicBlock*, int> bbRank_;
    void computeRanks(Function *func);
    int getRank(Value *v);

    // Core helpers
    void swapOperands(Instruction *inst);
public:
    static BinaryInst *createBinary(Instruction::OpID op, Value *v1, Value *v2,
                                     BasicBlock *bb, Instruction *before);
private:

    // Leaf collection: flatten associative op tree
    void collectLeafOperands(Value *root, Instruction::OpID op,
                             std::vector<Value*> &ops,
                             std::unordered_set<Value*> &visited);

    // Rebuild tree from flat operand list
    Value *rebuildAddTree(std::vector<Value*> &ops, BasicBlock *bb, Instruction *before);

    // Optimization
    Value *optAddTree(BinaryInst *root, std::vector<ValueEntry> &ops);

    // Main entry
    void reassociate(BinaryInst *inst);

    // Factor extraction from mul tree
    Value *removeFactor(Value *mulTree, Value *factor);
    void extractOneUseFactors(Value *v, std::vector<Value*> &factors);
};
