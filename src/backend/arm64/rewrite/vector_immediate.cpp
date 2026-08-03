// Shared AArch64 NEON splat-immediate classification and emission.
// See vector_immediate.hpp for the architectural role of this module.

#include "../../../include/backend/arm64/rewrite/vector_immediate.hpp"

namespace backend::aarch64 {
namespace {

bool isReplicatedByte(std::uint32_t lane, std::uint8_t &byte) {
    std::uint8_t low = static_cast<std::uint8_t>(lane & 0xffU);
    if (((lane >> 8) & 0xffU) != low ||
        ((lane >> 16) & 0xffU) != low ||
        ((lane >> 24) & 0xffU) != low)
        return false;
    byte = low;
    return true;
}

bool isFMov4sImmediate(std::uint32_t bits) {
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (mantissa & 0x7ffffu)
        return false;
    int exp = static_cast<int>((bits >> 23) & 0xffu) - 127;
    return exp >= -3 && exp <= 4;
}

} // namespace

std::optional<NeonSplatImmediate>
classifyNeonSplatImmediate(std::uint32_t laneBits) {
    if (laneBits == 0) {
        NeonSplatImmediate immediate;
        immediate.form = NeonSplatImmediate::Form::Zero;
        return immediate;
    }

    std::uint8_t byte = 0;
    if (isReplicatedByte(laneBits, byte)) {
        NeonSplatImmediate immediate;
        immediate.form = NeonSplatImmediate::Form::Movi16b;
        immediate.imm8 = byte;
        return immediate;
    }

    for (unsigned shift : {0U, 8U, 16U, 24U}) {
        std::uint32_t mask = 0xffU << shift;
        if ((laneBits & ~mask) == 0) {
            NeonSplatImmediate immediate;
            immediate.form = NeonSplatImmediate::Form::Movi4sLsl;
            immediate.imm8 =
                static_cast<std::uint8_t>((laneBits >> shift) & 0xffU);
            immediate.shift = static_cast<std::uint8_t>(shift);
            return immediate;
        }
    }
    for (unsigned msl : {8U, 16U}) {
        std::uint32_t ones = (1U << msl) - 1U;
        if ((laneBits & ones) != ones)
            continue;
        std::uint32_t high = laneBits >> msl;
        if (high > 0xffU)
            continue;
        if (((high << msl) | ones) != laneBits)
            continue;
        NeonSplatImmediate immediate;
        immediate.form = NeonSplatImmediate::Form::Movi4sMsl;
        immediate.imm8 = static_cast<std::uint8_t>(high);
        immediate.shift = static_cast<std::uint8_t>(msl);
        return immediate;
    }
    std::uint32_t inverted = ~laneBits;
    for (unsigned shift : {0U, 8U, 16U, 24U}) {
        std::uint32_t mask = 0xffU << shift;
        if ((inverted & ~mask) == 0) {
            NeonSplatImmediate immediate;
            immediate.form = NeonSplatImmediate::Form::Mvni4sLsl;
            immediate.imm8 =
                static_cast<std::uint8_t>((inverted >> shift) & 0xffU);
            immediate.shift = static_cast<std::uint8_t>(shift);
            return immediate;
        }
    }
    if (isFMov4sImmediate(laneBits)) {
        NeonSplatImmediate immediate;
        immediate.form = NeonSplatImmediate::Form::Fmov4s;
        immediate.bits = laneBits;
        return immediate;
    }
    return std::nullopt;
}

MachineInstr makeNeonSplatImmediate(const NeonSplatImmediate &immediate,
                                    MachineOperand destination) {
    switch (immediate.form) {
    case NeonSplatImmediate::Form::Zero: {
        MachineInstr instruction(Opcode::MOVIv4Zero);
        instruction.addOperand(destination);
        return instruction;
    }
    case NeonSplatImmediate::Form::Movi16b: {
        MachineInstr instruction(Opcode::MOVIv16b);
        instruction.addOperand(destination)
            .addOperand(MachineOperand::immediate(immediate.imm8));
        return instruction;
    }
    case NeonSplatImmediate::Form::Movi4sLsl: {
        MachineInstr instruction(Opcode::MOVIv4s);
        instruction.addOperand(destination)
            .addOperand(MachineOperand::immediate(immediate.imm8))
            .addOperand(MachineOperand::immediate(immediate.shift));
        return instruction;
    }
    case NeonSplatImmediate::Form::Movi4sMsl: {
        MachineInstr instruction(Opcode::MOVIv4sMsl);
        instruction.addOperand(destination)
            .addOperand(MachineOperand::immediate(immediate.imm8))
            .addOperand(MachineOperand::immediate(immediate.shift));
        return instruction;
    }
    case NeonSplatImmediate::Form::Mvni4sLsl: {
        MachineInstr instruction(Opcode::MVNIv4s);
        instruction.addOperand(destination)
            .addOperand(MachineOperand::immediate(immediate.imm8))
            .addOperand(MachineOperand::immediate(immediate.shift));
        return instruction;
    }
    case NeonSplatImmediate::Form::Fmov4s: {
        MachineInstr instruction(Opcode::FMOVv4s);
        instruction.addOperand(destination)
            .addOperand(MachineOperand::floatingBits(immediate.bits));
        return instruction;
    }
    }
    MachineInstr instruction(Opcode::MOVIv4Zero);
    instruction.addOperand(destination);
    return instruction;
}

} // namespace backend::aarch64
