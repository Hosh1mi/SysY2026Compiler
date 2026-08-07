#pragma once

#include "../ir/ir.hpp"
#include "loopInfo.hpp"

#include <optional>

class LazyValueInfo {
public:
    void analyze(Function *func, const LoopInfo *loopInfo = nullptr,
                 const DominatorTreeAnalysis *domTree = nullptr);

    Constant *getConstant(Value *value, Instruction *cxtI = nullptr);
    Constant *getConstantOnEdge(Value *value, BasicBlock *fromBB,
                                BasicBlock *toBB,
                                Instruction *cxtI = nullptr);

    std::optional<bool> getPredicateAt(ICmpInst::ICmpOp pred, Value *lhs,
                                       Value *rhs, Instruction *cxtI,
                                       bool useBlockValue = true);
    std::optional<bool> getPredicateOnEdge(ICmpInst::ICmpOp pred, Value *lhs,
                                           Value *rhs, BasicBlock *fromBB,
                                           BasicBlock *toBB,
                                           Instruction *cxtI = nullptr);

    void forgetValue(Value *value);
    void eraseBlock(BasicBlock *bb);
    void clear();

private:
    Function *function_ = nullptr;
    Module *module_ = nullptr;
    const LoopInfo *loopInfo_ = nullptr;
    const DominatorTreeAnalysis *domTree_ = nullptr;
};
