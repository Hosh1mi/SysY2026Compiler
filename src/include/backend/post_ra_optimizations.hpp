// Declares physical-register cleanup, pseudo expansion, and final addressing
// transforms that run after register allocation.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class PostRACopyPropagation {
public:
    bool run(MachineFunction &function) const;
};

class PostRARedundantCopyElimination {
public:
    bool run(MachineFunction &function) const;
};

class PostRAInstructionExpansion {
public:
    bool run(MachineFunction &function) const;
    bool expandConstantMaterializations(MachineFunction &function,
                                        bool enableMovn = true,
                                        bool enableLogicalImmediate = true) const;
};

class PostRAAddressingOptimizer {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
