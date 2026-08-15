// Declares physical-register cleanup, pseudo expansion, and final addressing
// transforms that run after register allocation.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class PostRACopyPropagation {
public:
    static bool run(MachineFunction &function);
};

class PostRARedundantCopyElimination {
public:
    static bool run(MachineFunction &function);
};

class A53FPRegisterBalancing {
public:
    static bool run(MachineFunction &function);
};

class PostRAInstructionExpansion {
public:
    static bool run(MachineFunction &function);
    static bool expandConstantMaterializations(MachineFunction &function);
};

class PostRAAddressingOptimizer {
public:
    static bool run(MachineFunction &function);
};

} // namespace backend::aarch64
