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
#include "spill_optimization.hpp"

namespace backend::aarch64 {

struct BackendOptions;

// Hold every MachineFunction transform used by the AArch64 pipeline so the
// driver can assemble the sequence once and reuse it across functions.
struct MachinePipelineServices {
  MachineConstantCSE constantCSE;
  AArch64VectorImmediateSelection vectorImmediateSelection;
  AArch64PreRAPeephole preRAPeephole;
  AArch64LoadStoreOptimization loadStoreOptimization;
  MachineSSALocalSink machineSSALocalSink;
  SinglePredecessorMaterializationSink materializationSink;
  PreRAAddressingFolder addressingFolder;
  AArch64ConditionOptimizer conditionOptimizer;
  DeadMachineInstructionElimination machineDCE;
  PostPhiConstantHoisting postPhiConstantHoisting;
  PhiElimination phiElimination;
  AArch64BranchFolding branchFolding;
  AArch64ExactHalvingLoopOptimizer exactHalvingLoopOptimizer;
  UnreachableMachineBlockElimination unreachableBlockElimination;
  A53MachineScheduler scheduler;
  GraphColoringRegisterAllocator registerAllocator;
  PostRASpillSlotOptimizer spillSlotOptimizer;
  PostRAParallelCopyResolver parallelCopyResolver;
  PostRAInstructionExpansion instructionExpansion;
  PostRACopyPropagation copyPropagation;
  A53FPRegisterBalancing fpRegisterBalancing;
  PostRARedundantCopyElimination redundantCopyElimination;
  PostRAAddressingOptimizer addressingOptimizer;
  MachineBlockPlacement blockPlacement;
  AArch64BranchRelaxation branchRelaxation;
  AArch64FrameLowering frameLowering;
};

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          MachinePipelineServices &services,
                          const BackendOptions &options);

} // namespace backend::aarch64
