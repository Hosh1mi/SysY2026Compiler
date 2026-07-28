#pragma once

#include "machine_ir.hpp"

#include <string>
#include <vector>

namespace backend::aarch64 {

struct VerificationError {
    std::string function;
    std::string block;
    unsigned instructionIndex = 0;
    std::string message;
};

class MachineVerifier {
public:
    std::vector<VerificationError> verify(const MachineFunction &function) const;
    void verifyOrThrow(const MachineFunction &function,
                       const std::string &stage) const;
};

} // namespace backend::aarch64
