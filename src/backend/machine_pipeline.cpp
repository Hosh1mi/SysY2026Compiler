// Assemble the staged AArch64 MachineFunction pipeline.
// Pass selection follows optimization level only — there are no per-pass
// disable switches; optional transforms are either in the O≥1 sequence or not.
#include "backend/machine_pipeline.hpp"

#include "backend/codegen.hpp"

namespace backend::aarch64 {
namespace {

void addPass(MachineFunctionPassManager &pipeline, std::string name,
             MachinePassStage stage, MachineProperty required,
             MachineProperty forbidden,
             MachineFunctionPassManager::PassRunner runner) {
  pipeline.addPass(std::move(name), stage, {required, forbidden},
                   std::move(runner));
}

template <typename Transform>
void addPreRAOptimization(MachineFunctionPassManager &pipeline,
                          Transform &transform, std::string name,
                          MachinePassStage stage,
                          MachineProperty required) {
  addPass(pipeline, std::move(name), stage, required,
          MachineProperty::NoVRegs,
          [&transform](MachineFunction &function) {
            return transform.run(function);
          });
}

void addMachineSSAOptimizations(MachineFunctionPassManager &pipeline,
                                MachinePipelineServices &services,
                                MachineProperty selectedSSA) {
  addPreRAOptimization(pipeline, services.constantCSE, "MachineConstantCSE",
                       MachinePassStage::MachineSSA, selectedSSA);
  addPreRAOptimization(pipeline, services.vectorImmediateSelection,
                       "AArch64VectorImmediateSelection",
                       MachinePassStage::MachineSSA, selectedSSA);
  addPreRAOptimization(pipeline, services.preRAPeephole,
                       "AArch64PreRAPeephole",
                       MachinePassStage::MachineSSA, selectedSSA);
  addPreRAOptimization(pipeline, services.loadStoreOptimization,
                       "AArch64LoadStoreOptimization",
                       MachinePassStage::MachineSSA, selectedSSA);
  addPreRAOptimization(pipeline, services.machineSSALocalSink,
                       "MachineSSALocalSink", MachinePassStage::MachineSSA,
                       selectedSSA);
  addPreRAOptimization(pipeline, services.materializationSink,
                       "SinglePredecessorMaterializationSink",
                       MachinePassStage::MachineSSA, selectedSSA);
}

void addPostPhiOptimizations(MachineFunctionPassManager &pipeline,
                             MachinePipelineServices &services,
                             MachineProperty selected) {
  addPreRAOptimization(pipeline, services.materializationSink,
                       "PostPhiMaterializationSink",
                       MachinePassStage::PreRegAlloc, selected);

  // PHI elimination creates edge blocks that act as preheaders for this
  // narrow constant-only transform.  General LICM belongs in Machine SSA
  // once loop canonicalization can provide preheaders there.
  addPass(pipeline, "PostPhiConstantHoisting",
          MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
          [&services](MachineFunction &function) {
            return services.postPhiConstantHoisting.run(function);
          });

  addPreRAOptimization(pipeline, services.constantCSE,
                       "MachineConstantCSEAfterHoisting",
                       MachinePassStage::PreRegAlloc, selected);
  addPreRAOptimization(pipeline, services.materializationSink,
                       "PostPhiMaterializationSinkAfterHoisting",
                       MachinePassStage::PreRegAlloc, selected);
}

} // namespace

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          MachinePipelineServices &services,
                          const BackendOptions &options) {
  const MachineProperty selected = MachineProperty::Selected;
  const MachineProperty selectedSSA =
      MachineProperty::Selected | MachineProperty::IsSSA;
  const MachineProperty allocated =
      MachineProperty::Selected | MachineProperty::NoVRegs;
  const MachineProperty finalized =
      allocated | MachineProperty::FrameFinalized;
  const bool optimize = options.optimizationLevel >= 1;

  if (optimize) {
    addMachineSSAOptimizations(pipeline, services, selectedSSA);
    addPass(pipeline, "AArch64PreRAAddressingFolder",
            MachinePassStage::MachineSSA, selectedSSA, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.addressingFolder.run(function);
            });
    addPass(pipeline, "AArch64ConditionOptimizer",
            MachinePassStage::MachineSSA, selectedSSA, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.conditionOptimizer.run(function);
            });
    addPass(pipeline, "MachineDCE", MachinePassStage::MachineSSA, selectedSSA,
            MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.machineDCE.run(function);
            });
  }

  addPass(pipeline, "PHIElimination", MachinePassStage::SSAElimination,
          selectedSSA, MachineProperty::NoVRegs,
          [&services](MachineFunction &function) {
            return services.phiElimination.run(function);
          });

  if (optimize) {
    addPostPhiOptimizations(pipeline, services, selected);
    addPass(pipeline, "AArch64BranchFolding",
            MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.branchFolding.run(function);
            });
    addPass(pipeline, "AArch64ExactHalvingLoopOptimizer",
            MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.exactHalvingLoopOptimizer.run(function);
            });
  }

  addPass(pipeline, "UnreachableMachineBlockElimination",
          MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
          [&services](MachineFunction &function) {
            return services.unreachableBlockElimination.run(function);
          });

  if (optimize)
    addPass(pipeline, "A53PreRAScheduler", MachinePassStage::PreRegAlloc,
            selected, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.scheduler.run(function);
            });

  addPass(pipeline, "GraphColoringRegisterAllocator",
          MachinePassStage::RegAlloc, selected, MachineProperty::NoVRegs,
          [&services](MachineFunction &function) {
            services.registerAllocator.run(function);
            return true;
          });
  addPass(pipeline, "PostRAParallelCopyResolver",
          MachinePassStage::PostRegAlloc, allocated,
          MachineProperty::FrameFinalized,
          [&services](MachineFunction &function) {
            return services.parallelCopyResolver.run(function);
          });
  addPass(pipeline, "AArch64FinalizeTiedOperands",
          MachinePassStage::PostRegAlloc, allocated,
          MachineProperty::FrameFinalized,
          [&services](MachineFunction &function) {
            return services.instructionExpansion.run(function);
          });
  addPass(pipeline, "PostRARedundantCopyElimination",
          MachinePassStage::PostRegAlloc, allocated,
          MachineProperty::FrameFinalized,
          [&services](MachineFunction &function) {
            return services.redundantCopyElimination.run(function);
          });
  if (optimize)
    addPass(pipeline, "PostRASpillSlotOptimizer",
            MachinePassStage::PostRegAlloc, allocated,
            MachineProperty::FrameFinalized,
            [&services](MachineFunction &function) {
              return services.spillSlotOptimizer.run(function);
            });
  if (optimize)
    addPass(pipeline, "PostRACopyPropagation", MachinePassStage::PostRegAlloc,
            allocated, MachineProperty::FrameFinalized,
            [&services](MachineFunction &function) {
              return services.copyPropagation.run(function);
            });

  addPass(pipeline, "AArch64FrameLowering",
          MachinePassStage::FrameFinalization, allocated,
          MachineProperty::FrameFinalized,
          [&services](MachineFunction &function) {
            services.frameLowering.run(function);
            return true;
          });
  if (optimize) {
    addPass(pipeline, "PostFrameCopyPropagation",
            MachinePassStage::FrameFinalization, finalized,
            MachineProperty::None,
            [&services](MachineFunction &function) {
              return services.copyPropagation.run(function);
            });
    addPass(pipeline, "AArch64PostRAAddressingOptimizer",
            MachinePassStage::FrameFinalization, finalized,
            MachineProperty::None,
            [&services](MachineFunction &function) {
              return services.addressingOptimizer.run(function);
            });
    addPass(pipeline, "MachineBlockPlacement",
            MachinePassStage::FrameFinalization, finalized,
            MachineProperty::None,
            [&services](MachineFunction &function) {
              return services.blockPlacement.run(function);
            });
  }
  addPass(pipeline, "AArch64ExpandConstantMaterialization",
          MachinePassStage::PreEmit, finalized, MachineProperty::None,
          [&services](MachineFunction &function) {
            return services.instructionExpansion.expandConstantMaterializations(
                function, /*enableMovn=*/true,
                /*enableLogicalImmediate=*/true);
          });
  if (optimize)
    addPass(pipeline, "A53PostRAScheduler", MachinePassStage::PreEmit,
            finalized, MachineProperty::None,
            [&services](MachineFunction &function) {
              return services.scheduler.run(function);
            });
  addPass(pipeline, "AArch64BranchRelaxation", MachinePassStage::PreEmit,
          finalized, MachineProperty::BranchesRelaxed,
          [&services](MachineFunction &function) {
            return services.branchRelaxation.run(function);
          });
}

} // namespace backend::aarch64
