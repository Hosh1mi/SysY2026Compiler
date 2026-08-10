// This file declares spill-slot forwarding and dead-store cleanup performed
// after physical registers have been assigned but before frame lowering.
#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class PostRASpillSlotOptimizer {
public:
  bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
