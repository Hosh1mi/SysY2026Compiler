#pragma once

#include "../../../../include/backend/arm64/machine.hpp"

bool tryMachinePostIndexScalar(MachineBasicBlock &block, size_t idx);
bool tryMachinePostIndexNeon(MachineBasicBlock &block, size_t idx);
