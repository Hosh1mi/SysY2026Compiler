// inlineExpand.hpp (修改部分)
#pragma once
#include "pass.hpp"
#include <unordered_map>

class InlineExpand : public Pass {
public:
    void execute(Module *module) override;
private:
    bool canInline(Function *callee, Function *caller);
    unsigned countInstructions(Function *func);
    // 返回内联过程中新产生的 call 指令列表
    std::vector<CallInst*> performInline(CallInst *callInst);
    std::vector<BasicBlock*> getRPO(Function *func);
    BasicBlock* splitBlockAfterCall(BasicBlock *callBB, CallInst *callInst);
    void cloneCalleeIntoCaller(Function *callee, Function *caller,
                               std::vector<Value*> &args,
                               std::unordered_map<Value*, Value*> &valMap,
                               std::unordered_map<BasicBlock*, BasicBlock*> &bbMap,
                               std::vector<BasicBlock*> &newBBs);
};

static int INLINE_THRESHOLD = 30;