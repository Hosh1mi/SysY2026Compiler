#pragma once

#include "liveness.hpp"

bool machineDCE(MachineFunction &func, const MachineLivenessResult &liveness);
