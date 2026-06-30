#pragma once
#include "../ir/ir.hpp"
#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"
#include <set>
#include <vector>
#include <unordered_map>

// LoopVectorize（pipeline 中暂停用，见 main.cpp / plan 阶段 5）。
// 循环结构统一来自 LoopInfo（plan 阶段 3.1）：latch/exit 经
// singleLatch()/singleExit() 推导。
class LoopVectorize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopVectorize"; }

private:
    // ── Induction variable ──────────────────────────────────────────────
    struct InductionVar {
        PhiInst      *phi;             // header phi
        Value        *initVal;         // initial value (from preheader)
        int           stride;          // constant stride
        bool          isAdd;           // true for add, false for sub
        Instruction  *updateInst;      // the add/sub instruction in latch
    };

    struct ReductionGroup {
        struct PointerPhi {
            PhiInst *phi;
            Value   *initVal;
            int      step;
        };

        PhiInst       *accPhi;         // loop-carried reduction phi
        Value         *initVal;        // initial scalar accumulator
        std::vector<Value*> contributionTerms; // scalar terms independent of accPhi
        std::vector<PointerPhi> pointerPhis;   // pointer recurrences used by terms
        bool           isAdd = false;  // true: acc += contribution; false: acc -= contribution
    };

    // ── Memory access description ──────────────────────────────────────
    struct MemAccess {
        enum Kind { LOAD, STORE };
        Kind       kind;
        Instruction *inst;             // load or store instruction
        Value      *basePtr;           // base pointer of the GEP chain
        int         elementOffset;     // constant offset from base (in elements)
    };

    // ── Per-function driver ────────────────────────────────────────────
    void runOnFunction(Function *func, const BasicAliasAnalysis &BAA);

    // ── Loop analysis helpers ──────────────────────────────────────────
    bool findInductionVar(const Loop &loop, InductionVar &iv);
    bool analyzeReductionLoop(const Loop &loop, const InductionVar &iv,
                              ReductionGroup &group);
    bool analyzeStrideAccesses(const Loop &loop, const InductionVar &iv,
                               std::vector<MemAccess> &loads,
                               std::vector<MemAccess> &stores);
    bool isLoopInvariant(Value *val, const std::set<BasicBlock*> &loopBlocks);
    Value *getBasePtr(Value *ptr);

    // ── Vectorization ──────────────────────────────────────────────────
    bool hasSafeMemoryDependencies(const Loop &loop,
                                   const std::vector<MemAccess> &loads,
                                   const std::vector<MemAccess> &stores,
                                   const BasicAliasAnalysis &BAA);
    bool tryVectorize(Loop &loop, Function *func, Module *module,
                      const BasicAliasAnalysis &BAA);
    void emitReductionVectorizedLoop(const Loop &loop, const InductionVar &iv,
                                     const ReductionGroup &group,
                                     int vecWidth, Function *func,
                                     Module *module);
    void emitVectorizedLoop(const Loop &loop, const InductionVar &iv,
                            const std::vector<MemAccess> &loads,
                            const std::vector<MemAccess> &stores,
                            int vecWidth,
                            Function *func, Module *module);
};
