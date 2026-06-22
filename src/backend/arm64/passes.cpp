#include "../../include/backend/arm64/passes.hpp"

#include "../../include/backend/arm64/machineDCE.hpp"
#include "../../include/backend/arm64/machineTransforms.hpp"
#include "../../include/backend/arm64/scheduler.hpp"

bool Arm64MachineDCEPass::run(MachineFunction &function) {
    return machineDCE(function);
}

bool Arm64CopyPropagationPass::run(MachineFunction &function) {
    return runMachineCopyPropagation(function);
}

bool Arm64InstructionCombinePass::run(MachineFunction &function) {
    return runMachineInstructionCombine(function);
}

bool Arm64MemoryOptimizationPass::run(MachineFunction &function) {
    return runMachineMemoryOptimization(function);
}

bool Arm64BranchOptimizationPass::run(MachineFunction &function) {
    return runMachineBranchOptimization(function);
}

bool Arm64PeepholePass::run(MachineFunction &function) {
    return runMachinePeephole(function);
}

bool Arm64PostRASchedulerPass::run(MachineFunction &function) {
    MachineScheduler().schedule(function);
    return true;
}
