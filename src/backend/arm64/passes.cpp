#include "../../include/backend/arm64/passes.hpp"

#include "../../include/backend/arm64/machineDCE.hpp"
#include "../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../include/backend/arm64/scheduler.hpp"

bool Arm64MachineDCEPass::run(MachineFunction &function,
                              MachineAnalysisManager &analyses) {
    return machineDCE(function, analyses.liveness(function));
}

bool Arm64CopyPropagationPass::run(MachineFunction &function,
                                   MachineAnalysisManager &analyses) {
    return runMachineCopyPropagation(function, analyses.liveness(function));
}

bool Arm64InstructionCombinePass::run(MachineFunction &function,
                                      MachineAnalysisManager &analyses) {
    return runMachineInstructionCombine(function, analyses.liveness(function));
}

bool Arm64CodeMotionPass::run(MachineFunction &function,
                              MachineAnalysisManager &analyses) {
    return runMachineCodeMotion(function, analyses.liveness(function));
}

bool Arm64MemoryOptimizationPass::run(MachineFunction &function,
                                      MachineAnalysisManager &analyses) {
    return runMachineMemoryOptimization(function, analyses.liveness(function));
}

bool Arm64CFGOptimizationPass::run(MachineFunction &function,
                                   MachineAnalysisManager &) {
    return runMachineCFGOptimization(function);
}

bool Arm64BitBranchOptimizationPass::run(MachineFunction &function,
                                         MachineAnalysisManager &analyses) {
    return runMachineBitBranchOptimization(function, analyses.liveness(function));
}

bool Arm64CanonicalizationPass::run(MachineFunction &function,
                                    MachineAnalysisManager &) {
    return runMachineCanonicalization(function);
}

bool Arm64LocalCSEPass::run(MachineFunction &function,
                            MachineAnalysisManager &) {
    return runMachineLocalCSE(function);
}

bool Arm64PeepholePass::run(MachineFunction &function,
                            MachineAnalysisManager &) {
    return runMachinePeephole(function);
}

bool Arm64PostRASchedulerPass::run(MachineFunction &function,
                                   MachineAnalysisManager &) {
    MachineScheduler().schedule(function);
    return true;
}
