#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class AlgebraSimplify : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "AlgebraSimplify"; }

private:
    void runOnFunction(Function *func);
    void runOnBasicBlock(BasicBlock *bb);
    bool tryAlgebraicSimplification(Instruction *inst);
    bool tryStrengthReduction(Instruction *inst);

    Value* getConstantZero(Type *ty);
    Value* getConstantOne(Type *ty);
    Value* getConstantAllOnes(Type *ty);
    static bool isPowerOfTwo(int v);
    static int  log2Int(int v);
};