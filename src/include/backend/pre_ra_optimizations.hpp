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

// Shorten local SSA live ranges without crossing a control-flow edge.
class MachineSSALocalSink {
public:
    bool run(MachineFunction &function) const;
};

// Move a single-use materialization across a proven single-predecessor edge.
class SinglePredecessorMaterializationSink {
public:
    bool run(MachineFunction &function) const;
};

class PreRAAddressingFolder {
public:
    bool run(MachineFunction &function) const;
};

class AArch64ConditionOptimizer {
public:
    bool run(MachineFunction &function) const;
};

class DeadMachineInstructionElimination {
public:
    bool run(MachineFunction &function) const;
};

// Hoist scalar constants into preheaders created during PHI elimination.
class PostPhiConstantHoisting {
public:
    bool run(MachineFunction &function) const;
};

class AArch64BranchFolding {
public:
    bool run(MachineFunction &function) const;
};

class AArch64ExactHalvingLoopOptimizer {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
