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

} // namespace

// To isolate a pass, remove or restore its addPass registration below.
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
		addPass(pipeline, "MachineConstantCSE", MachinePassStage::MachineSSA,
		        selectedSSA, MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.constantCSE.run(function);
		        });
		addPass(pipeline, "AArch64VectorImmediateSelection",
		        MachinePassStage::MachineSSA, selectedSSA,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.vectorImmediateSelection.run(function);
		        });
		addPass(pipeline, "AArch64PreRAPeephole", MachinePassStage::MachineSSA,
		        selectedSSA, MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.preRAPeephole.run(function);
		        });
		addPass(pipeline, "AArch64LoadStoreOptimization",
		        MachinePassStage::MachineSSA, selectedSSA,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.loadStoreOptimization.run(function);
		        });
		addPass(pipeline, "MachineSSALocalSink", MachinePassStage::MachineSSA,
		        selectedSSA, MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.machineSSALocalSink.run(function);
		        });
		addPass(pipeline, "SinglePredecessorMaterializationSink",
		        MachinePassStage::MachineSSA, selectedSSA,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.materializationSink.run(function);
		        });
		addPass(pipeline, "AArch64PreRAAddressingFolder",
		        MachinePassStage::MachineSSA, selectedSSA,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.addressingFolder.run(function);
		        });
		addPass(pipeline, "AArch64ConditionOptimizer",
		        MachinePassStage::MachineSSA, selectedSSA,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.conditionOptimizer.run(function);
		        });
		addPass(pipeline, "MachineExpressionCSE", MachinePassStage::MachineSSA,
		        selectedSSA, MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.expressionCSE.run(function);
		        });
		addPass(pipeline, "MachineDCE", MachinePassStage::MachineSSA,
		        selectedSSA, MachineProperty::NoVRegs,
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
		addPass(pipeline, "PostPhiMaterializationSink",
		        MachinePassStage::PreRegAlloc, selected,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.materializationSink.run(function);
		        });

		// PHI elimination creates edge blocks that act as preheaders for this
		// narrow constant-only transform.  General LICM belongs in Machine SSA
		// once loop canonicalization can provide preheaders there.
		addPass(pipeline, "PostPhiConstantHoisting",
		        MachinePassStage::PreRegAlloc, selected,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.postPhiConstantHoisting.run(function);
		        });

		addPass(pipeline, "MachineConstantCSEAfterHoisting",
		        MachinePassStage::PreRegAlloc, selected,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.constantCSE.run(function);
		        });
		addPass(pipeline, "PostPhiMaterializationSinkAfterHoisting",
		        MachinePassStage::PreRegAlloc, selected,
		        MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.materializationSink.run(function);
		        });
		addPass(pipeline, "AArch64BranchFolding", MachinePassStage::PreRegAlloc,
		        selected, MachineProperty::NoVRegs,
		        [&services](MachineFunction &function) {
			        return services.branchFolding.run(function);
		        });
		addPass(pipeline, "AArch64ExactHalvingLoopOptimizer",
		        MachinePassStage::PreRegAlloc, selected,
		        MachineProperty::NoVRegs,
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
	if (optimize) {
		addPass(pipeline, "PostRACopyPropagation",
		        MachinePassStage::PostRegAlloc, allocated,
		        MachineProperty::FrameFinalized,
		        [&services](MachineFunction &function) {
			        return services.copyPropagation.run(function);
		        });
		addPass(pipeline, "PostRAFinalCopyCleanup",
		        MachinePassStage::PostRegAlloc, allocated,
		        MachineProperty::FrameFinalized,
		        [&services](MachineFunction &function) {
			        return services.redundantCopyElimination.run(function);
		        });
	}

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
		        MachineProperty::None, [&services](MachineFunction &function) {
			        return services.copyPropagation.run(function);
		        });
		addPass(pipeline, "AArch64PostRAAddressingOptimizer",
		        MachinePassStage::FrameFinalization, finalized,
		        MachineProperty::None, [&services](MachineFunction &function) {
			        return services.addressingOptimizer.run(function);
		        });
		addPass(pipeline, "MachineBlockPlacement",
		        MachinePassStage::FrameFinalization, finalized,
		        MachineProperty::None, [&services](MachineFunction &function) {
			        return services.blockPlacement.run(function);
		        });
	}
	addPass(
	    pipeline, "AArch64ExpandConstantMaterialization",
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
