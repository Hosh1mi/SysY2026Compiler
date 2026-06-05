#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <map>
#include <set>
#include <vector>
#include <unordered_map>

class LoopVectorize : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopVectorize"; }

private:
    // ── Loop representation ─────────────────────────────────────────────
    struct Loop {
        BasicBlock *header;
        BasicBlock *latch;
        BasicBlock *preheader;        // single predecessor outside the loop
        BasicBlock *exitBB;           // unique exit block
        std::set<BasicBlock*> blocks; // all blocks in the loop
    };

    // ── Induction variable ──────────────────────────────────────────────
    struct InductionVar {
        PhiInst      *phi;             // header phi
        Value        *initVal;         // initial value (from preheader)
        int           stride;          // constant stride (must be > 0)
        bool          isAdd;           // true for add, false for sub
        Instruction  *updateInst;      // the add/sub instruction in latch
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
    void runOnFunction(Function *func);

    // ── Loop detection ─────────────────────────────────────────────────
    std::vector<Loop> findLoops(Function *func);

    // ── Loop analysis helpers ──────────────────────────────────────────
    bool findInductionVar(const Loop &loop, InductionVar &iv);
    bool analyzeStrideAccesses(const Loop &loop, const InductionVar &iv,
                               std::vector<MemAccess> &loads,
                               std::vector<MemAccess> &stores);
    bool isLoopInvariant(Value *val, const std::set<BasicBlock*> &loopBlocks);
    Value *getBasePtr(Value *ptr);

    // ── Vectorization ──────────────────────────────────────────────────
    bool tryVectorize(Loop &loop, Function *func, Module *module);
    void emitVectorizedLoop(const Loop &loop, const InductionVar &iv,
                            const std::vector<MemAccess> &loads,
                            const std::vector<MemAccess> &stores,
                            int vecWidth,
                            Function *func, Module *module);

    // ── Dominator state ────────────────────────────────────────────────
    DominatorInfo *domInfo_ = nullptr;
};
