// This file declares generated AArch64 instruction queries.
// Generated from aarch64.td by generate.py.
// Edit the target description or generator, not this file.
#pragma once

#include "backend/target.hpp"

#include <cstdint>

namespace backend::aarch64::generated {

enum class ImmediateConstraint : std::uint8_t {
    AddSub12,
    Logical32,
    None,
    Shift32,
    Shift64,
};

const InstrDesc &instructionDescriptor(Opcode opcode);
ImmediateConstraint immediateConstraint(Opcode opcode);
bool isCommutable(Opcode opcode);
unsigned rematerializationCost(Opcode opcode);

} // namespace backend::aarch64::generated
