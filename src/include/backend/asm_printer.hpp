#pragma once

#include "machine_ir.hpp"

#include <ostream>

namespace backend::aarch64 {

class AArch64AssemblyPrinter {
public:
    void printFunction(const MachineFunction &function,
                       std::ostream &output) const;
};

} // namespace backend::aarch64
