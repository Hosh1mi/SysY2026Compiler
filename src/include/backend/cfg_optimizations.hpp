// Declares late machine-CFG cleanup, layout, and branch-range transforms.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class UnreachableMachineBlockElimination {
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
