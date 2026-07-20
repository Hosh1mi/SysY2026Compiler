#pragma once

#include "affineAnalysis.hpp"
#include "loopInfo.hpp"

#include <vector>

struct LoopMemoryAccess {
    Instruction       *inst = nullptr;
    Value             *ptr = nullptr;
    GetElementPtrInst *gep = nullptr;

    bool isLoad() const { return inst && inst->is_load(); }
    bool isStore() const { return inst && inst->is_store(); }
};

struct LoopAccessInfo {
    std::vector<LoopMemoryAccess>    memory_accesses;
    std::vector<Instruction *>       memory_instructions;
    std::vector<GetElementPtrInst *> memory_geps;
    std::vector<GetElementPtrInst *> all_geps;
    bool                            has_call = false;
    bool                            has_store = false;
};

class LoopAccessAnalysis {
public:
    explicit LoopAccessAnalysis(AffineAnalysis &AA) : AA_(&AA) {}

    LoopAccessInfo collect(Loop *loop) const;

    bool isAffineOverAncestorIVs(GetElementPtrInst *gep, Loop *inner) const;

    static bool isGlobalOrArgument(Value *value);
    static int innermostArrayDim(Value *base);

private:
    AffineAnalysis *AA_;
};
