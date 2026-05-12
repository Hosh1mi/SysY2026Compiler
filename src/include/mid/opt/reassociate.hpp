#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Reassociate : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

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
