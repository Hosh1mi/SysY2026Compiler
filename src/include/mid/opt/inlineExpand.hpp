#pragma once
#include "pass.hpp"
#include <map>

class InlineExpand : public Pass {
public:
    void execute(Module *module) override;

private:
    int inlineIdCounter = 0; 
    bool runOnFunction(Function *func, Module *module);
    bool shouldInline(Function *callee, Function *caller);
    bool inlineCall(CallInst *call, Function *callee, Module *module);
    Instruction* cloneInstruction(Instruction *instr, BasicBlock *newBB,
                                  std::map<Value*, Value*> &valueMap,
                                  std::map<BasicBlock*, BasicBlock*> &bbMap,
                                  Module *module);
};