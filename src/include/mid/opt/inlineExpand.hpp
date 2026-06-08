#pragma once
#include "pass.hpp"
#include <unordered_map>

class InlineExpand : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "InlineExpand"; }
private:
    bool canInline(CallInst *call, Function *callee, Function *caller,
                   int recursiveBudget);
    unsigned countCallSites(Function *callee, Module *module);
    unsigned countInstructions(Function *func);
    bool isSelfRecursive(Function *func);
    bool hasNonSelfCalls(Function *func);
    bool isCallInLoop(CallInst *call);
    int estimateInlineCost(Function *func);
    int estimateRecursiveInlineBudget(int weightedCost, bool callInLoop,
                                      int foldBenefit);
    int weighInstruction(Instruction *inst);
    int estimateConstantFoldBenefit(CallInst *call, Function *callee);
    std::vector<CallInst*> performInline(CallInst *callInst);
    std::vector<BasicBlock*> getRPO(Function *func);
    BasicBlock* splitBlockAfterCall(BasicBlock *callBB, CallInst *callInst);
    void cloneCalleeIntoCaller(Function *callee, Function *caller,
                               std::vector<Value*> &args,
                               std::unordered_map<Value*, Value*> &valMap,
                               std::unordered_map<BasicBlock*, BasicBlock*> &bbMap,
                               std::vector<BasicBlock*> &newBBs);
};

constexpr int INLINE_THRESHOLD = 80;
constexpr int INLINE_ALWAYS_THRESHOLD = 6;
constexpr int INLINE_RECURSIVE_THRESHOLD = 40;
constexpr int INLINE_RECURSIVE_HOT_COST = 45;
constexpr int INLINE_COST_BUDGET = 300;
constexpr int CALL_OVERHEAD = 8;
constexpr int LOOP_MULTIPLIER = 5;
