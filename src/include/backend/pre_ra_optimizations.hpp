// Declares virtual-register and machine-SSA optimizations that run before
// graph-coloring register allocation.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class MachineConstantCSE {
public:
    bool run(MachineFunction &function) const;
};

class AArch64VectorImmediateSelection {
public:
    bool run(MachineFunction &function) const;
};

class AArch64PreRAPeephole {
public:
    bool run(MachineFunction &function) const;
};

class AArch64LoadStoreOptimization {
public:
    bool run(MachineFunction &function) const;
};

class MachineSink {
public:
    bool run(MachineFunction &function) const;
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
