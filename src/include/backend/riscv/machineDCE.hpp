#pragma once

#include "machine.hpp"

namespace riscv {

bool eliminateDeadMachineInstructions(MFunction &func);

}  // namespace riscv
