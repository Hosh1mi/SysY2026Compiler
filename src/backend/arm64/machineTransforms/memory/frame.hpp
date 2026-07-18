#pragma once

#include "../../../../include/backend/arm64/liveness.hpp"
#include "../../../../include/backend/arm64/machine.hpp"

bool tryMachineZeroStore(MachineBasicBlock &block, size_t idx,
                         const MachineLivenessResult &liveness);
bool tryMachineStoreLoadForward(MachineBasicBlock &block, size_t idx);
bool tryMachineDeadStore(MachineBasicBlock &block, size_t idx);
bool tryMachineMergeStores(MachineBasicBlock &block, size_t idx);
bool tryMachineMergeLoads(MachineBasicBlock &block, size_t idx);
