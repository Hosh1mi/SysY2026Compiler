#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

// Split multi-dimensional GEPs inside loops at the invariant/variant boundary.
// The invariant prefix is hoisted by LICM, leaving only the variant suffix
// in the loop body — drastically reducing address computation cost.
class SplitGEP : public Pass {
public:
    void execute(Module *module) override;

private:
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;             // single back-edge source
        std::set<BasicBlock*> blocks;  // all blocks in the loop body
    };

    void runOnFunction(Function *func);
    std::vector<Loop> findLoops(Function *func);
    bool isVariant(Value *v, const std::set<BasicBlock*> &loopBlocks,
                   std::set<Value*> &variantCache,
                   std::set<Value*> &invariantCache);
    bool runOnLoop(const Loop &loop);
};
