// Declares late machine-CFG cleanup, layout, and branch-range transforms.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class UnreachableMachineBlockElimination {
public:
    static bool run(MachineFunction &function);
};

class MachineBlockPlacement {
public:
    static bool run(MachineFunction &function);
};

class AArch64BranchRelaxation {
public:
    static bool run(MachineFunction &function);
};

} // namespace backend::aarch64
