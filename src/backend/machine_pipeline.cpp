// Assemble the native MachineFunction pass list.  The order here is the
// backend contract; keep it linear so phase transitions stay visible.
#include "backend/machine_pipeline.hpp"

#include "backend/cfg_optimizations.hpp"
#include "backend/codegen.hpp"
#include "backend/frame_lowering.hpp"
#include "backend/post_ra_optimizations.hpp"
#include "backend/pre_ra_optimizations.hpp"
#include "backend/regalloc.hpp"
#include "backend/scheduler.hpp"
#include "backend/spill_optimization.hpp"

namespace backend::aarch64 {
namespace {

bool runPhysicalCleanup(MachineFunction &function) {
	bool changed = false;
	for (;;) {
		bool roundChanged = false;
		roundChanged |= PostRASpillSlotOptimizer::run(function);
		roundChanged |= PostRACopyPropagation::run(function);
		roundChanged |= PostRARedundantCopyElimination::run(function);
		if (!roundChanged)
			return changed;
		changed = true;
	}
}

} // namespace

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          const BackendOptions &options) {
	const bool optimize = options.optimizationLevel >= 1;

	if (optimize) {
		pipeline.addPass("MachineConstantCSE", &MachineConstantCSE::run);
		pipeline.addPass("AArch64VectorImmediateSelection",
		                 &AArch64VectorImmediateSelection::run);
		pipeline.addPass("AArch64PreRAPeephole",
		                 &AArch64PreRAPeephole::run);
		pipeline.addPass("AArch64LoadStoreOptimization",
		                 &AArch64LoadStoreOptimization::run);
		pipeline.addPass("MachineSSALocalSink", &MachineSSALocalSink::run);
		pipeline.addPass("SinglePredecessorMaterializationSink",
		                 &SinglePredecessorMaterializationSink::run);
		pipeline.addPass("AArch64PreRAAddressingFolder",
		                 &PreRAAddressingFolder::run);
		pipeline.addPass("AArch64ConditionOptimizer",
		                 &AArch64ConditionOptimizer::run);
		pipeline.addPass("MachineExpressionCSE", &MachineExpressionCSE::run);
		pipeline.addPass("MachineDCE", &DeadMachineInstructionElimination::run);
	}

	pipeline.addPass("PHIElimination", &PhiElimination::run);

	if (optimize) {
		pipeline.addPass("PostPhiMaterializationSink",
		                 &SinglePredecessorMaterializationSink::run);
		pipeline.addPass("PostPhiConstantHoisting", &PostPhiConstantHoisting::run);

		pipeline.addPass("MachineConstantCSEAfterHoisting",
		                 &MachineConstantCSE::run);
		pipeline.addPass("PostPhiMaterializationSinkAfterHoisting",
		                 &SinglePredecessorMaterializationSink::run);
		pipeline.addPass("AArch64BranchFolding", &AArch64BranchFolding::run);
		pipeline.addPass("AArch64ExactHalvingLoopOptimizer",
		                 &AArch64ExactHalvingLoopOptimizer::run);
	}

	pipeline.addPass("UnreachableMachineBlockElimination",
	                 &UnreachableMachineBlockElimination::run);

	if (optimize)
		pipeline.addPass("A53PreRAScheduler", &A53MachineScheduler::run);

	pipeline.addPass("GraphColoringRegisterAllocator",
	                 &GraphColoringRegisterAllocator::run);
	pipeline.addPass("PostRAParallelCopyResolver",
	                 &PostRAParallelCopyResolver::run);
	pipeline.addPass("AArch64FinalizeTiedOperands",
	                 &PostRAInstructionExpansion::run);
	if (optimize) {
		pipeline.addPass("PostRAPhysicalCleanup", &runPhysicalCleanup);
		pipeline.addPass("A53FPRegisterBalancing",
		                 &A53FPRegisterBalancing::run);
	} else {
		pipeline.addPass("PostRARedundantCopyElimination",
		                 &PostRARedundantCopyElimination::run);
	}

	pipeline.addPass("AArch64FrameLowering", &AArch64FrameLowering::run);
	if (optimize) {
		pipeline.addPass("AArch64PostRAAddressingOptimizer",
		                 &PostRAAddressingOptimizer::run);
		pipeline.addPass("MachineBlockPlacement", &MachineBlockPlacement::run);
	}
	pipeline.addPass("AArch64ExpandConstantMaterialization",
	                 &PostRAInstructionExpansion::expandConstantMaterializations);
	if (optimize)
		pipeline.addPass("A53PostRAScheduler", &A53MachineScheduler::run);
	pipeline.addPass("AArch64BranchRelaxation", &AArch64BranchRelaxation::run);
}

} // namespace backend::aarch64
