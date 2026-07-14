#pragma once

#include "../../../../include/backend/arm64/liveness.hpp"
#include "../../../../include/backend/arm64/machine.hpp"

bool tryMachineWidenI32CopyWindow(MachineFunction &func,
                                  MachineBasicBlock &block,
                                  size_t idx,
                                  const MachineLivenessResult &liveness);
