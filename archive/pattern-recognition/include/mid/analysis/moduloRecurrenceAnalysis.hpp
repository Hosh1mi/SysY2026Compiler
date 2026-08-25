#pragma once

#include "../ir/ir.hpp"

#include <set>
#include <vector>

namespace ModuloRecurrenceAnalysis {

struct Bounds {
    long long lower = 0;
    long long upper = 0;
};

struct SignedTerm {
    Value *value = nullptr;
    int sign = 1;
    Bounds bounds;
    bool hasBounds = false;
};

struct Recurrence {
    PhiInst *state = nullptr;
    BinaryInst *remainder = nullptr;
    ConstantInt *modulus = nullptr;
    std::vector<SignedTerm> contributionTerms;
    std::set<Instruction *> updateChain;
    Bounds contributionRange;
    bool hasContributionRange = false;
};

bool dependsOn(Value *value, Value *target);

// Infer a conservative, non-wrapping signed-i32 interval.  Failure means the
// caller must keep the original scalar operation order.
bool inferBounds(Value *value, Bounds &bounds);

// Recognize
//   state.next = (state + term0 - term1 + ...) % positive_constant
// across the supplied update blocks.  The state coefficient must be exactly
// +1; every other state-dependent expression is rejected.
bool analyze(PhiInst *state, BinaryInst *remainder,
             const std::set<BasicBlock *> &updateBlocks,
             Recurrence &result);

// The recurrence chain may feed its state phi, but no other instruction in
// the transformed region.  Callers that repair live-outs may explicitly
// allow loop-external uses.
bool hasPrivateUpdateChain(const Recurrence &recurrence,
                           const std::set<BasicBlock *> &updateBlocks,
                           bool allowExternalUses = false);

bool inferContributionBounds(Recurrence &recurrence,
                             const std::vector<PhiInst *> &loopStates,
                             PhiInst *inductionState);

// Advance an interval through the recurrence's contribution expression.
// inferContributionBounds() must have populated every term's bounds first.
bool advanceBounds(Bounds &bounds, const Recurrence &recurrence,
                   unsigned repetitions = 1);

// Prove that the original signed-i32 update expression cannot wrap.  This
// also performs and caches the contribution range analysis used by clients.
bool proveNoI32UpdateWrap(Recurrence &recurrence,
                          const std::vector<PhiInst *> &loopStates,
                          PhiInst *inductionState, Value *initial);

bool fitsSignedI32(long long lower, long long upper);
bool needsAtMostOneCorrection(long long lower, long long upper,
                              long long modulus);

} // namespace ModuloRecurrenceAnalysis
