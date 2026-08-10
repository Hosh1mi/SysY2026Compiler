// Owns machine-level transform instances and registers them onto a staged
// MachineFunctionPassManager, mirroring how the mid-end builds its pipeline.
#pragma once

#include "frame_lowering.hpp"
#include "cfg_optimizations.hpp"
#include "machine_pass_manager.hpp"
#include "post_ra_optimizations.hpp"
#include "pre_ra_optimizations.hpp"
#include "regalloc.hpp"
#include "scheduler.hpp"

namespace backend::aarch64 {

struct BackendOptions;

// Hold every MachineFunction transform used by the AArch64 pipeline so the
// driver can assemble the sequence once and reuse it across functions.
struct MachinePipelineServices {
  AArch64PreRAOptimizer preRAOptimizer;
  DeadMachineInstructionElimination machineDCE;
  MachineInvariantConstantMotion invariantConstantMotion;
  AArch64ConditionOptimizer conditionOptimizer;
  PhiElimination phiElimination;
  PreRACFGOptimizer preRACFGOptimizer;
  UnreachableMachineBlockElimination unreachableBlockElimination;
  A53MachineScheduler scheduler;
  GraphColoringRegisterAllocator registerAllocator;
  PostRAParallelCopyResolver parallelCopyResolver;
  PostRAInstructionExpansion instructionExpansion;
  PostRACopyPropagation copyPropagation;
  PostRARedundantCopyElimination redundantCopyElimination;
  PreRAAddressingFolder addressingFolder;
  PostRAAddressingOptimizer addressingOptimizer;
  MachineBlockPlacement blockPlacement;
  AArch64BranchRelaxation branchRelaxation;
  AArch64FrameLowering frameLowering;
};

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          MachinePipelineServices &services,
                          const BackendOptions &options);

} // namespace backend::aarch64
