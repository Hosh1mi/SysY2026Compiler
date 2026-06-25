#pragma once

#include "pass.hpp"

#include <memory>
#include <vector>

class Arm64MachinePassManager {
public:
    void addPass(std::unique_ptr<Arm64MachineFunctionPass> pass);
    bool run(MachineFunction &function);

private:
    std::vector<std::unique_ptr<Arm64MachineFunctionPass>> passes_;
};

class Arm64MachineOptimizationPipeline {
public:
    Arm64MachineOptimizationPipeline(bool enableOptimizations,
                                     bool enableScheduling);
    void run(MachineFunction &function);

private:
    bool enableOptimizations_;
    Arm64MachinePassManager cleanup_;
    Arm64MachinePassManager optimizations_;
    Arm64MachinePassManager scheduler_;
};

