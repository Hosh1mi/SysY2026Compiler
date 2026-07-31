#pragma once

#include "../ir/ir.hpp"

#include <set>
#include <vector>

namespace ModuloRecurrenceAnalysis {

struct SignedTerm {
    Value *value = nullptr;
    int sign = 1;
};

struct Recurrence {
    PhiInst *state = nullptr;
    BinaryInst *remainder = nullptr;
    ConstantInt *modulus = nullptr;
    std::vector<SignedTerm> contributionTerms;
    std::set<Instruction *> updateChain;
};

bool dependsOn(Value *value, Value *target);

// Infer a conservative, non-wrapping signed-i32 interval.  Failure means the
// caller must keep the original scalar operation order.
bool inferBounds(Value *value, long long &lower, long long &upper);

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

bool contributionBounds(const Recurrence &recurrence,
                        const std::vector<PhiInst *> &loopStates,
                        PhiInst *inductionState,
                        long long &lower, long long &upper);

bool fitsSignedI32(long long lower, long long upper);
bool needsAtMostOneCorrection(long long lower, long long upper,
                              long long modulus);

} // namespace ModuloRecurrenceAnalysis
