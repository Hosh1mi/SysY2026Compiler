#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class PreRAMachinePeephole {
public:
    bool run(MachineFunction &function) const;
};

class DeadMachineInstructionElimination {
public:
    bool run(MachineFunction &function) const;
};

class MachineLICM {
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

class PostRACopyPropagation {
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

} // namespace backend::aarch64
