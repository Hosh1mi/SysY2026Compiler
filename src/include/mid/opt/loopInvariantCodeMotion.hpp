#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>

class LICM : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LICM"; }

private:
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;
        std::set<BasicBlock*> blocks;
    };

    void runOnFunction(Function *func);
    void eliminateSinglePredPhis(Function *func);
    void eliminateTrivialHeaderPhis(const Loop &loop);

    std::vector<Loop> findLoops(Function *func);
    BasicBlock *getPreheader(const Loop &loop);
    bool isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                     const std::set<Instruction*>& toHoist, const Loop *loop = nullptr);
    bool isSafeToHoist(Instruction *inst, const Loop &loop,
                       const std::set<Instruction*>& toHoist, bool loopHasCalls);
    bool hasStoreToSameBase(const Loop &loop, Value *base);
    static bool doesAnyCallWriteToBase(const Loop &loop, Value *base);
    bool runOnLoop(const Loop &loop);

    DominatorInfo *domInfo_ = nullptr;
};
