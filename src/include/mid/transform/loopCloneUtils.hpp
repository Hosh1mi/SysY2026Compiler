#pragma once
// Shared helpers for cloning loop-region instructions and remapping values
// across peeled / unrolled copies.

#include "../analysis/loopInfo.hpp"
#include "../ir/instruction.hpp"

#include <unordered_map>
#include <unordered_set>

namespace loop_clone {

// Remap `v` through `valueMap`.  Loop-invariant values (defined outside
// `loopBlocks`, or non-instructions) are returned unchanged.  Loop-local
// values missing from the map yield nullptr — callers must treat that as a
// hard failure rather than silently reusing the old definition.
Value *remapValueOrInvariant(Value *v,
                             const std::unordered_map<Value *, Value *> &valueMap,
                             const std::unordered_set<BasicBlock *> &loopBlocks);

// Clone a non-phi, non-terminator instruction into `destBB`, remapping
// operands with remapValueOrInvariant.  Returns nullptr if the opcode is
// unsupported or an operand fails to remap.
Instruction *cloneInstruction(Instruction *orig, BasicBlock *destBB,
                              std::unordered_map<Value *, Value *> &valueMap,
                              const std::unordered_set<BasicBlock *> &loopBlocks);

// Append a remapped incoming edge to an exit phi for a newly cloned
// predecessor.  `originalPred` is the edge being mirrored; `clonedPred` is
// the new predecessor block.
bool addRemappedIncomingForClonedEdge(
    PhiInst *phi, BasicBlock *originalPred, BasicBlock *clonedPred,
    const std::unordered_map<Value *, Value *> &valueMap,
    const std::unordered_set<BasicBlock *> &loopBlocks);

Value *incomingFrom(PhiInst *phi, BasicBlock *pred);

bool removeIncomingFrom(PhiInst *phi, BasicBlock *pred);

} // namespace loop_clone
