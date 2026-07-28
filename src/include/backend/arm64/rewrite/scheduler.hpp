#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class A53MachineScheduler {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
