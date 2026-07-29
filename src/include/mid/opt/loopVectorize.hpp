#pragma once

#include "../analysis/loopVectorizationAnalysis.hpp"
#include "pass.hpp"

#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

class LoopVectorize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopVectorize"; }

private:
    struct InductionVar {
        PhiInst *phi = nullptr;
        Value *initVal = nullptr;
        int stride = 0;
        bool isAdd = true;
        Instruction *updateInst = nullptr;
    };

    struct PackedOperand {
        enum Kind { CONTIGUOUS, IV_STEP, GATHER, INVARIANT };
        Kind kind;
        Type *scalarTy;
        Value *source;
        int laneStride;
    };

    struct ReductionGroup {
        enum Kind { Add, Sub, SMin, SMax };
        PhiInst *accPhi;
        Value *initVal;
        Value *latchValue;
        PackedOperand lhs;
        PackedOperand rhs;
        // Optional expression-form reduction description.  The legacy
        // two-operand form leaves this empty and uses lhs/rhs directly.
        std::vector<std::pair<LoadInst *, PackedOperand>> expressionLoads;
        std::vector<Value *> expressionTerms;
        bool expressionReduction = false;
        int scalarStep;
        Kind kind = Add;
        bool isAdd = false;
        bool noMul = false;
    };

    void runOnFunction(Function *func, const BasicAliasAnalysis &BAA);
    bool findInductionVar(const Loop &loop, InductionVar &iv);
    bool analyzeReductionLoop(const Loop &loop, const InductionVar &iv,
                              ReductionGroup &group);
    bool isLoopInvariant(Value *val,
                         const std::set<BasicBlock *> &loopBlocks);

    bool tryVectorize(Loop &loop, Function *func, Module *module,
                      const BasicAliasAnalysis &BAA);
    void emitReductionVectorizedLoop(const Loop &loop, const InductionVar &iv,
                                     const ReductionGroup &group,
                                     int vecWidth, Function *func,
                                     Module *module);
    bool emitVectorizedLoop(const LoopVectorizationAnalysis::Plan &plan,
                            Function *func, Module *module);
};
