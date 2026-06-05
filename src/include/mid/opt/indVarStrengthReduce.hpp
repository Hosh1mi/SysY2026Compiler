#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"
#include <map>
#include <set>
#include <vector>

class IndVarStrengthReduce : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "IndVarStrengthReduce"; }

private:
    void runOnFunction(Function *func);

    struct Loop {
        BasicBlock *header;
        std::set<BasicBlock *> blocks;
        BasicBlock *latch;      // block with back edge to header
        BasicBlock *preheader;  // predecessor outside the loop
    };

    struct BasicIV {
        PhiInst *phi;       // header phi
        Value *initVal;     // initial value
        Value *stride;      // constant stride
        bool isAdd;         // true for add, false for sub
        BasicBlock *latch;  // block with update instruction
        Instruction *updateInst; // the add/sub instruction
    };

    // dominator info (from Function)
    DominatorInfo *domInfo_ = nullptr;

    // loop detection
    std::vector<Loop> findLoops(Function *func);

    // loop-invariant test
    bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &loopBlocks);

    // basic IV detection
    std::vector<BasicIV> findBasicIVs(Loop &loop);

    // ensure preheader exists, return it
    BasicBlock *ensurePreheader(Loop &loop, Function *func, Module *module);

    // main strength reduction routine
    void processLoop(Loop &loop, Function *func, Module *module);
};
