#pragma once
#include "machine.hpp"

/// Apply low-risk peephole optimizations directly on MachineInstr.
void peepholeOptimize(MachineFunction &func);
