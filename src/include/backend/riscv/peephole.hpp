#pragma once

#include "machine.hpp"

namespace riscv {

bool removeSelfMoves(MFunction &func);
bool forwardAdjacentStoreLoads(MFunction &func);

}  // namespace riscv
