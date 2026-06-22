#pragma once

#include "pass.hpp"

class Arm64MachineDCEPass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-machine-dce"; }
    bool run(MachineFunction &function) override;
};

class Arm64CopyPropagationPass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-copy-propagation"; }
    bool run(MachineFunction &function) override;
};

class Arm64InstructionCombinePass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-instruction-combine"; }
    bool run(MachineFunction &function) override;
};

class Arm64MemoryOptimizationPass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-memory-optimization"; }
    bool run(MachineFunction &function) override;
};

class Arm64BranchOptimizationPass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-branch-optimization"; }
    bool run(MachineFunction &function) override;
};

class Arm64PeepholePass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-peephole"; }
    bool run(MachineFunction &function) override;
};

class Arm64PostRASchedulerPass final : public Arm64MachineFunctionPass {
public:
    const char *name() const override { return "arm64-post-ra-scheduler"; }
    bool run(MachineFunction &function) override;
};

