#pragma once
#include "../liveness.hpp"
#include "../machine.hpp"

// Machine transform rule groups. Each entry point performs at most one
// rewrite; the pass pipeline owns convergence and phase ordering.
bool runMachineCopyPropagation(MachineFunction &func,
                               const MachineLivenessResult &liveness);
bool runMachineInstructionCombine(MachineFunction &func,
                                  const MachineLivenessResult &liveness);
bool runMachineCodeMotion(MachineFunction &func,
                          const MachineLivenessResult &liveness);
bool runMachineMemoryOptimization(MachineFunction &func,
                                  const MachineLivenessResult &liveness);
bool runMachineCFGOptimization(MachineFunction &func);
bool runMachineBitBranchOptimization(MachineFunction &func,
                                     const MachineLivenessResult &liveness);
bool runMachineCanonicalization(MachineFunction &func);
bool runMachineLocalCSE(MachineFunction &func);
bool runMachinePeephole(MachineFunction &func);
