#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class A53MachineScheduler {
public:
    static bool run(MachineFunction &function);
};

} // namespace backend::aarch64
