#pragma once
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/scalarEvolution.hpp"
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

    void runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    void eliminateSinglePredPhis(Function *func);
    void eliminateTrivialHeaderPhis(const Loop &loop);

    std::vector<Loop> findLoops(Function *func);
    BasicBlock *getPreheader(const Loop &loop);
    bool isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                     const std::set<Instruction*>& toHoist, const Loop *loop = nullptr,
                     ScalarEvolution *SE = nullptr, ::Loop *analysisLoop = nullptr);
    bool isSafeToHoist(Instruction *inst, const Loop &loop,
                       const BasicAliasAnalysis &BAA);
    bool runOnLoop(const Loop &loop, ScalarEvolution *SE = nullptr,
                   ::Loop *analysisLoop = nullptr,
                   const BasicAliasAnalysis *BAA = nullptr);

    DominatorInfo *domInfo_ = nullptr;
};
