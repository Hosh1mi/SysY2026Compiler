#pragma once
// InlineExpand —— 基于代价模型的函数内联与有限自递归展开。
//
// 将合适的调用点替换为 callee 体，暴露跨函数优化机会。
//
// 典型支持形式：
//   小函数 / 热路径上的普通调用 → 内联
//   有限次自递归调用点的受控展开
//
// 超出代价阈值或不安全的调用保持 call。成功后调用点变为内联后的
// 控制流与 SSA。

#include "pass.hpp"
#include <unordered_map>
#include <unordered_set>

class InlineExpand : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "InlineExpand"; }
private:
    bool canInline(CallInst *call, Function *callee, Function *caller,
                   int recursiveBudget);
    unsigned countCallSites(Function *callee, Module *module);
    unsigned countInstructions(Function *func);
    bool isRecursive(Function *func);
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
    bool isReachableFromEntry(Function *target, Module *module);
    bool reaches(Function *from, Function *target,
                 std::unordered_set<Function *> &visited);

    std::unordered_map<Function *, bool> recursiveCache_;
};

constexpr int INLINE_THRESHOLD = 80;
constexpr int INLINE_ALWAYS_THRESHOLD = 6;
constexpr int INLINE_RECURSIVE_THRESHOLD = 40;
constexpr int INLINE_RECURSIVE_HOT_COST = 45;
constexpr int INLINE_COST_BUDGET = 300;
constexpr int CALL_OVERHEAD = 8;
constexpr int LOOP_MULTIPLIER = 5;
