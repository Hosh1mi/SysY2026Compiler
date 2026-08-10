// Declares virtual-register and machine-SSA optimizations that run before
// graph-coloring register allocation.
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
    bool run(MachineFunction &function, PreRAOptimizationKind kind) const;
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

class PreRAAddressingFolder {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
