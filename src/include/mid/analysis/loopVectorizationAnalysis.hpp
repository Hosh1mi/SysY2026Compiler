#pragma once

#include "basicAliasAnalysis.hpp"
#include "loopInfo.hpp"

#include <string>
#include <unordered_map>
#include <vector>

// A target-aware, transformation-independent description of a loop that can
// be widened.  The analysis deliberately describes every memory access: an
// unclassified access is a legality failure, never an instruction for the
// emitter to silently scalarize.
class LoopVectorizationAnalysis {
public:
    static constexpr int DefaultVF = 4;

    struct Induction {
        PhiInst *phi = nullptr;
        Value *init = nullptr;
        BinaryInst *update = nullptr;
        Value *bound = nullptr;
        ICmpInst *compare = nullptr;
        int step = 0;
    };

    struct PointerRecurrence {
        PhiInst *phi = nullptr;
        Value *init = nullptr;
        GetElementPtrInst *update = nullptr;
        int step = 0;
    };

    enum class AccessKind { Load, Store };
    enum class AddressKind { InductionGEP, PointerRecurrence, Uniform };

    struct MemoryAccess {
        AccessKind kind = AccessKind::Load;
        AddressKind addressKind = AddressKind::InductionGEP;
        Instruction *inst = nullptr;
        Type *scalarType = nullptr;

        // InductionGEP: clone gep and replace varyingIndex by vector IV plus
        // ivOffset.  The varying dimension must advance by one scalar element.
        GetElementPtrInst *gep = nullptr;
        unsigned varyingIndex = 0;
        int ivOffset = 0;

        // PointerRecurrence: use the widened pointer phi plus pointerOffset.
        PhiInst *pointerPhi = nullptr;
        int pointerOffset = 0;

        // Non-uniform accesses in the same group have provably identical
        // lane-wise addresses and may share the widened pointer expression.
        // The group is formed from the normalized address description above.
        size_t addressGroup = 0;
        Value *underlyingObject = nullptr;
        int programOrder = 0;
    };

    // A pair of contiguous memory ranges whose independence could not be
    // proved statically.  The transformation may version the loop with a
    // range-overlap check and enter the vector loop only on the disjoint path.
    struct RuntimeMemoryCheck {
        size_t firstAccess = 0;
        size_t secondAccess = 0;
    };

    struct Plan {
        Loop *loop = nullptr;
        BasicBlock *preheader = nullptr;
        BasicBlock *header = nullptr;
        BasicBlock *body = nullptr;
        BasicBlock *latch = nullptr;
        BasicBlock *exit = nullptr;
        Induction induction;
        std::vector<PointerRecurrence> pointerRecurrences;
        std::vector<Instruction *> recipes;
        std::vector<MemoryAccess> memoryAccesses;
        std::vector<RuntimeMemoryCheck> runtimeMemoryChecks;
        std::unordered_map<Instruction *, size_t> accessForInst;
        int vectorWidth = DefaultVF;
        int unrollFactor = 1;
        bool canDeferStoresAcrossParts = false;
        int scalarCost = 0;
        int vectorCost = 0;
    };

    explicit LoopVectorizationAnalysis(const BasicAliasAnalysis &BAA)
        : BAA_(BAA) {}

    bool buildPlan(Loop &loop, Plan &plan, std::string *reason = nullptr) const;

private:
    bool reject(std::string *reason, const char *message) const;
    bool isLoopInvariant(Value *value, const Loop &loop) const;
    bool findInduction(Loop &loop, Plan &plan, std::string *reason) const;
    bool findPointerRecurrences(Plan &plan, std::string *reason) const;
    bool classifyMemory(Plan &plan, std::string *reason) const;
    bool checkInstructions(Plan &plan, std::string *reason) const;
    bool checkMemoryDependences(Plan &plan, std::string *reason) const;
    bool checkProfitability(Plan &plan, std::string *reason) const;

    const BasicAliasAnalysis &BAA_;
};
