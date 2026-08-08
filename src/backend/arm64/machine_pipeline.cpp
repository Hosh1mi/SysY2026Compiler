// Assemble the staged AArch64 MachineFunction pipeline.
// Pass selection follows optimization level only — there are no per-pass
// disable switches; optional transforms are either in the O≥1 sequence or not.
#include "../../include/backend/arm64/machine_pipeline.hpp"

#include "../../include/backend/arm64/codegen.hpp"

namespace backend::aarch64 {
namespace {

void addPass(MachineFunctionPassManager &pipeline, std::string name,
             MachinePassStage stage, MachineProperty required,
             MachineProperty forbidden,
             MachineFunctionPassManager::PassRunner runner) {
  pipeline.addPass(std::move(name), stage, {required, forbidden},
                   std::move(runner));
}

void addPreRAOptimizations(MachineFunctionPassManager &pipeline,
                           MachinePipelineServices &services,
                           MachinePassStage stage, MachineProperty required,
                           const std::string &suffix) {
  auto addOptimization = [&](const std::string &name,
                             PreRAOptimizationKind kind) {
    addPass(pipeline, name + suffix, stage, required, MachineProperty::NoVRegs,
            [&services, kind](MachineFunction &function) {
              return services.preRAOptimizer.run(function, kind);
            });
  };
  addOptimization("MachineConstantCSE", PreRAOptimizationKind::ConstantCSE);
  addOptimization("AArch64VectorImmediateSelection",
                  PreRAOptimizationKind::VectorImmediateSelection);
  addOptimization("AArch64PreRAPeephole", PreRAOptimizationKind::Peephole);
  addOptimization("AArch64LoadStoreOptimization",
                  PreRAOptimizationKind::LoadStoreOptimization);
  addOptimization("MachineSink", PreRAOptimizationKind::MachineSink);
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
    addPreRAOptimizations(pipeline, services, MachinePassStage::MachineSSA,
                          selectedSSA, "");
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
              bool changed = false;
              for (unsigned iteration = 0; iteration < 4; ++iteration) {
                const bool localChange = services.machineDCE.run(function);
                changed |= localChange;
                if (!localChange)
                  break;
              }
              return changed;
            });
  }

  addPass(pipeline, "PHIElimination", MachinePassStage::SSAElimination,
          selectedSSA, MachineProperty::NoVRegs,
          [&services](MachineFunction &function) {
            return services.phiElimination.run(function);
          });

  if (optimize) {
    addPreRAOptimizations(pipeline, services, MachinePassStage::PreRegAlloc,
                          selected, "AfterPHI");
    addPass(pipeline, "MachineInvariantConstantMotion",
            MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.invariantConstantMotion.run(function);
            });
    addPreRAOptimizations(pipeline, services, MachinePassStage::PreRegAlloc,
                          selected, "AfterLICM");
    addPass(pipeline, "AArch64PreRACFGOptimizer",
            MachinePassStage::PreRegAlloc, selected, MachineProperty::NoVRegs,
            [&services](MachineFunction &function) {
              return services.preRACFGOptimizer.run(function);
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
