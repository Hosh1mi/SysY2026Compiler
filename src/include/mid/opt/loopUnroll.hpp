#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class LoopUnroll : public Pass {
public:
    void execute(Module *module) override;

private:
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;
        std::set<BasicBlock *> blocks;
    };

    void runOnFunction(Function *func);
    bool tryUnroll(Loop &loop, Function *func, Module *module);
    Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                           const std::unordered_map<Value *, Value *> &vmap);

    std::vector<Loop> findLoops(Function *func);
    BasicBlock *getPreheader(const Loop &loop);

    DominatorInfo *domInfo_ = nullptr;
};
