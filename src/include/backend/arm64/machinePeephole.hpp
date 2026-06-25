#pragma once
#include "machine.hpp"

// Machine transform rule groups. Each entry point performs at most one
// rewrite; the pass pipeline owns convergence and phase ordering.
bool runMachineCopyPropagation(MachineFunction &func);
bool runMachineInstructionCombine(MachineFunction &func);
bool runMachineCodeMotion(MachineFunction &func);
bool runMachineMemoryOptimization(MachineFunction &func);
bool runMachineBranchOptimization(MachineFunction &func);
bool runMachineCanonicalization(MachineFunction &func);
bool runMachineLocalCSE(MachineFunction &func);
bool runMachinePeephole(MachineFunction &func);
