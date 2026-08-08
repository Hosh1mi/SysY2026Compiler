#pragma once

#include "machine_ir.hpp"

#include <ostream>
#include <string>

namespace backend::aarch64 {

class AArch64AssemblyPrinter {
public:
    void printFunction(const MachineFunction &function,
                       std::ostream &output) const;
    std::string printFunction(const MachineFunction &function) const;
};

} // namespace backend::aarch64
