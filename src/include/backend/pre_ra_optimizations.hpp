// Declares virtual-register and machine-SSA optimizations that run before
// graph-coloring register allocation.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class MachineConstantCSE {
public:
    static bool run(MachineFunction &function);
};

class MachineExpressionCSE {
public:
    static bool run(MachineFunction &function);
};

class AArch64VectorImmediateSelection {
public:
    static bool run(MachineFunction &function);
};

class AArch64PreRAPeephole {
public:
    static bool run(MachineFunction &function);
};

class AArch64LoadStoreOptimization {
public:
    static bool run(MachineFunction &function);
};

// Shorten local SSA live ranges without crossing a control-flow edge.
class MachineSSALocalSink {
public:
    static bool run(MachineFunction &function);
};

// Move a single-use materialization across a proven single-predecessor edge.
class SinglePredecessorMaterializationSink {
public:
    static bool run(MachineFunction &function);
};

class PreRAAddressingFolder {
public:
    static bool run(MachineFunction &function);
};

class AArch64ConditionOptimizer {
public:
    static bool run(MachineFunction &function);
};

class DeadMachineInstructionElimination {
public:
    static bool run(MachineFunction &function);
};

// Hoist scalar constants into preheaders created during PHI elimination.
class PostPhiConstantHoisting {
public:
    static bool run(MachineFunction &function);
};

class AArch64BranchFolding {
public:
    static bool run(MachineFunction &function);
};

class AArch64ExactHalvingLoopOptimizer {
public:
    static bool run(MachineFunction &function);
};

} // namespace backend::aarch64
