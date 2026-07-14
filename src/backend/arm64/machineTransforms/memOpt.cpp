#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "memory/frame.hpp"
#include "memory/postIndex.hpp"
#include "memory/wideCopy.hpp"

bool runMachineMemoryOptimization(MachineFunction &func) {
	MachineLivenessResult liveness = MachineLiveness().analyze(func);
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineWidenI32CopyWindow(func, block, i, liveness) ||
			    tryMachineStoreLoadForward(block, i) ||
			    tryMachineZeroStore(block, i, liveness) ||
			    tryMachineDeadStore(block, i) ||
			    tryMachinePostIndexScalar(block, i) ||
			    tryMachinePostIndexNeon(block, i) ||
			    tryMachineMergeStores(block, i) ||
			    tryMachineMergeLoads(block, i))
				return true;
		}
	}
	return false;
}
