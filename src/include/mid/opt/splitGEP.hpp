#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

class SplitGEP : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "SplitGEP"; }

private:
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;
        std::set<BasicBlock*> blocks;
    };

    void runOnFunction(Function *func);
    std::vector<Loop> findLoops(Function *func);
    bool isVariant(Value *v, const std::set<BasicBlock*> &loopBlocks,
                   std::set<Value*> &variantCache,
                   std::set<Value*> &invariantCache);
    bool runOnLoop(const Loop &loop);
};
