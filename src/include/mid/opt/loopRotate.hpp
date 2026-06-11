#pragma once

#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

#include <map>
#include <vector>

class LoopRotate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopRotate"; }

private:
    bool runOnFunction(Function *func);
    bool rotateLoop(Loop *loop, Function *func);

    Instruction *cloneInstruction(Instruction *inst, BasicBlock *dest,
                                  const std::map<Value *, Value *> &valueMap);
    BasicBlock *splitExitEdge(Function *func, BasicBlock *pred,
                              BasicBlock *exit);
};
