#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

enum class PreRAOptimizationKind {
    ConstantCSE,
    VectorImmediateSelection,
    Peephole,
    LoadStoreOptimization,
    MachineSink,
};

class AArch64PreRAOptimizer {
public:
    bool run(MachineFunction &function,
             PreRAOptimizationKind kind) const;
};

class DeadMachineInstructionElimination {
public:
    bool run(MachineFunction &function) const;
};

class MachineInvariantConstantMotion {
public:
    bool run(MachineFunction &function) const;
};

class AArch64ConditionOptimizer {
public:
    bool run(MachineFunction &function) const;
};

class PreRACFGOptimizer {
public:
    bool run(MachineFunction &function) const;
};

class UnreachableMachineBlockElimination {
public:
    bool run(MachineFunction &function) const;
};

class PostRACopyPropagation {
public:
    bool run(MachineFunction &function) const;
};

class PostRAInstructionExpansion {
public:
    // Lower vector insert/accumulate into tied post-RA form.
    bool run(MachineFunction &function) const;
    // Expand MOVi32/MOVi64 into MOVZ/MOVK pieces so the post-RA scheduler
    // can interleave independent constant materializations.
    bool expandConstantMaterializations(MachineFunction &function) const;
};

// Fold ADDXri / ADDXrs address arithmetic into LDR/STR scaled-immediate or
// register-offset forms while virtual registers remain.  Dead address defs are
// left for Machine DCE.
class PreRAAddressingFolder {
public:
    bool run(MachineFunction &function) const;
};

class PostRAAddressingOptimizer {
public:
    bool run(MachineFunction &function) const;
};

class MachineBlockPlacement {
public:
    bool run(MachineFunction &function) const;
};

class AArch64BranchRelaxation {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
