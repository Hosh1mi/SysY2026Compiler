// Shared AArch64 NEON splat-immediate classification and emission.
//
// ISel materializes vector-typed constants; the pre-RA peephole decides when a
// scalar immediate that only feeds DUPs should move to the NEON bank.  Both
// paths must share one encoding policy so GPR/NEON choice is not duplicated.

#pragma once

#include "machine_ir.hpp"
#include "target.hpp"

#include <cstdint>
#include <optional>

namespace backend::aarch64 {

struct NeonSplatImmediate {
    enum class Form : std::uint8_t {
        Zero,
        Movi4sLsl,
        Movi4sMsl,
        Mvni4sLsl,
        Movi16b,
        Fmov4s,
    };

    Form form = Form::Zero;
    std::uint8_t imm8 = 0;
    std::uint8_t shift = 0;
    std::uint32_t bits = 0;
};

std::optional<NeonSplatImmediate>
classifyNeonSplatImmediate(std::uint32_t laneBits);

MachineInstr makeNeonSplatImmediate(const NeonSplatImmediate &immediate,
                                    MachineOperand destination);

} // namespace backend::aarch64
