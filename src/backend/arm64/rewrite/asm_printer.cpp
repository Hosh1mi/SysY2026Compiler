#include "../../../include/backend/arm64/rewrite/asm_printer.hpp"

#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace backend::aarch64 {
namespace {

std::string conditionName(CondCode condition) {
    static const std::array<const char *, 15> names = {
        "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
        "hi", "ls", "ge", "lt", "gt", "le", "al"};
    unsigned index = static_cast<unsigned>(condition);
    return index < names.size() ? names[index] : "al";
}

std::string blockLabel(const MachineFunction &function,
                       const MachineBasicBlock *block) {
    return ".L" + function.name() + "_bb" +
           std::to_string(block->number());
}

std::string registerName(const MachineOperand &operand) {
    if (!operand.isPhysicalRegister())
        throw std::logic_error("assembly register operand is not physical");
    return std::string(RegisterInfo::name(operand.physicalRegister(),
                                          operand.regClass()));
}

std::string registerNameAs(const MachineOperand &operand,
                           RegClass view) {
    if (!operand.isPhysicalRegister())
        throw std::logic_error(
            "assembly register operand is not physical");
    return std::string(RegisterInfo::name(
        operand.physicalRegister(), view));
}

void emitMemory(std::ostream &output, const char *mnemonic,
                const MachineInstr &instruction) {
    std::string value = registerName(instruction.operands()[0]);
    if (instruction.operands()[0].regClass() == RegClass::NEON128)
        value = "q" + value.substr(1);
    std::int64_t offset = instruction.operands()[2].immediate();
    unsigned width =
        instruction.operands()[0].regClass() == RegClass::NEON128 ? 16
        : instruction.operands()[0].regClass() == RegClass::GPR64 ? 8
                                                                  : 4;
    std::string base = registerName(instruction.operands()[1]);
    bool scaledEncodable =
        offset >= 0 && offset % width == 0 &&
        static_cast<std::uint64_t>(offset / width) <= 4095;
    if (!scaledEncodable)
        throw std::logic_error(
            "illegal memory offset reached assembly printer");
    output << '\t' << mnemonic << ' ' << value << ", [" << base;
    if (offset)
        output << ", #" << offset;
    output << "]\n";
}

void emitGlobalMemory(std::ostream &output, const char *mnemonic,
                      const MachineInstr &instruction) {
    if (instruction.operands().size() != 3 ||
        !instruction.operands()[0].isPhysicalRegister() ||
        !instruction.operands()[1].isPhysicalRegister() ||
        instruction.operands()[2].kind() !=
            MachineOperand::Kind::GlobalSymbol)
        throw std::logic_error("malformed global memory instruction");
    std::string value = registerName(instruction.operands()[0]);
    if (instruction.operands()[0].regClass() == RegClass::NEON128)
        value = "q" + value.substr(1);
    output << '\t' << mnemonic << ' ' << value << ", ["
           << registerName(instruction.operands()[1]) << ", :lo12:"
           << instruction.operands()[2].symbol() << "]\n";
}

void emitDMemory(std::ostream &output, const char *mnemonic,
                 const MachineInstr &instruction) {
    std::string value = registerName(instruction.operands()[0]);
    value = "d" + value.substr(1);
    std::int64_t offset = instruction.operands()[2].immediate();
    bool scaledEncodable =
        offset >= 0 && offset % 8 == 0 &&
        static_cast<std::uint64_t>(offset / 8) <= 4095;
    if (!scaledEncodable)
        throw std::logic_error(
            "illegal D-register memory offset reached assembly printer");
    output << '\t' << mnemonic << ' ' << value << ", ["
           << registerName(instruction.operands()[1]);
    if (offset)
        output << ", #" << offset;
    output << "]\n";
}

void emitPostIndexedMemory(std::ostream &output, const char *mnemonic,
                           const MachineInstr &instruction) {
    std::string value = registerName(instruction.operands()[0]);
    if (instruction.operands()[0].regClass() == RegClass::NEON128)
        value = "q" + value.substr(1);
    output << '\t' << mnemonic << ' ' << value << ", ["
           << registerName(instruction.operands()[1]) << "], #"
           << instruction.operands()[2].immediate() << '\n';
}

// Mirror ADDXrs extend encoding: 0 = uxtw, 1 = sxtw, 2 = lsl (64-bit index).
void emitRegisterOffsetMemory(std::ostream &output, const char *mnemonic,
                              const MachineInstr &instruction) {
    if (instruction.operands().size() != 5 ||
        !instruction.operands()[0].isPhysicalRegister() ||
        !instruction.operands()[1].isPhysicalRegister() ||
        !instruction.operands()[2].isPhysicalRegister() ||
        instruction.operands()[3].kind() !=
            MachineOperand::Kind::Immediate ||
        instruction.operands()[4].kind() !=
            MachineOperand::Kind::Immediate)
        throw std::logic_error("malformed register-offset memory instruction");
    std::string value = registerName(instruction.operands()[0]);
    if (instruction.operands()[0].regClass() == RegClass::NEON128)
        value = "q" + value.substr(1);
    std::int64_t shift = instruction.operands()[3].immediate();
    std::int64_t extension = instruction.operands()[4].immediate();
    unsigned width =
        instruction.operands()[0].regClass() == RegClass::NEON128 ? 16
        : instruction.operands()[0].regClass() == RegClass::GPR64 ? 8
                                                                  : 4;
    unsigned legalShift = 0;
    while ((1U << legalShift) < width)
        ++legalShift;
    if (shift != 0 && shift != static_cast<std::int64_t>(legalShift))
        throw std::logic_error(
            "illegal register-offset shift reached assembly printer");
    const char *extend =
        extension == 2 ? "lsl" : extension ? "sxtw" : "uxtw";
    output << '\t' << mnemonic << ' ' << value << ", ["
           << registerName(instruction.operands()[1]) << ", "
           << registerName(instruction.operands()[2]) << ", " << extend;
    if (shift)
        output << " #" << shift;
    output << "]\n";
}

void emitThreeRegisters(std::ostream &output, const char *mnemonic,
                        const MachineInstr &instruction) {
    output << '\t' << mnemonic << ' '
           << registerName(instruction.operands()[0]) << ", "
           << registerName(instruction.operands()[1]) << ", "
           << registerName(instruction.operands()[2]) << '\n';
}

void emitThreeWithImmediate(std::ostream &output, const char *mnemonic,
                            const MachineInstr &instruction) {
    output << '\t' << mnemonic << ' '
           << registerName(instruction.operands()[0]) << ", "
           << registerName(instruction.operands()[1]) << ", #"
           << instruction.operands()[2].immediate() << '\n';
}

std::string vectorView(const MachineOperand &operand) {
    return registerName(operand) + ".4s";
}

void printInstruction(const MachineFunction &function,
                      const MachineInstr &instruction,
                      std::ostream &output) {
    const auto &operands = instruction.operands();
    switch (instruction.opcode()) {
    case Opcode::COPY: {
        if (operands.size() != 2)
            throw std::logic_error("malformed COPY at assembly printing");
        std::string destination = registerName(operands[0]);
        std::string source = registerName(operands[1]);
        if (operands[0].regClass() == RegClass::NEON128)
            output << "\tmov " << destination << ".16b, "
                   << source << ".16b\n";
        else if (operands[0].regClass() == RegClass::FPR32 ||
                 operands[1].regClass() == RegClass::FPR32)
            output << "\tfmov " << destination << ", " << source << '\n';
        else
            output << "\tmov " << destination << ", " << source << '\n';
        break;
    }
    case Opcode::MOVZ: {
        if (operands.size() != 3 ||
            !operands[0].isPhysicalRegister() ||
            operands[1].kind() != MachineOperand::Kind::Immediate ||
            operands[2].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed movz at assembly printing");
        output << "\tmovz " << registerName(operands[0]) << ", #"
               << operands[1].immediate();
        if (operands[2].immediate())
            output << ", lsl #" << operands[2].immediate();
        output << '\n';
        break;
    }
    case Opcode::MOVK: {
        if (operands.size() != 4 ||
            !operands[0].isPhysicalRegister() ||
            !operands[1].isPhysicalRegister() ||
            operands[2].kind() != MachineOperand::Kind::Immediate ||
            operands[3].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed movk at assembly printing");
        output << "\tmovk " << registerName(operands[0]) << ", #"
               << operands[2].immediate();
        if (operands[3].immediate())
            output << ", lsl #" << operands[3].immediate();
        output << '\n';
        break;
    }
    case Opcode::MOVIv4Zero:
        output << "\tmovi " << registerName(operands[0])
               << ".4s, #0\n";
        break;
    case Opcode::MOVIv4s: {
        if (operands.size() != 3 ||
            operands[1].kind() != MachineOperand::Kind::Immediate ||
            operands[2].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed movi.4s");
        output << "\tmovi " << registerName(operands[0]) << ".4s, #"
               << operands[1].immediate();
        if (operands[2].immediate())
            output << ", lsl #" << operands[2].immediate();
        output << '\n';
        break;
    }
    case Opcode::MOVIv4sMsl: {
        if (operands.size() != 3 ||
            operands[1].kind() != MachineOperand::Kind::Immediate ||
            operands[2].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed movi.4s msl");
        output << "\tmovi " << registerName(operands[0]) << ".4s, #"
               << operands[1].immediate() << ", msl #"
               << operands[2].immediate() << '\n';
        break;
    }
    case Opcode::MVNIv4s: {
        if (operands.size() != 3 ||
            operands[1].kind() != MachineOperand::Kind::Immediate ||
            operands[2].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed mvni.4s");
        output << "\tmvni " << registerName(operands[0]) << ".4s, #"
               << operands[1].immediate();
        if (operands[2].immediate())
            output << ", lsl #" << operands[2].immediate();
        output << '\n';
        break;
    }
    case Opcode::MOVIv16b: {
        if (operands.size() != 2 ||
            operands[1].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed movi.16b");
        output << "\tmovi " << registerName(operands[0])
               << ".16b, #" << operands[1].immediate() << '\n';
        break;
    }
    case Opcode::FMOVv4s: {
        if (operands.size() != 2 ||
            operands[1].kind() != MachineOperand::Kind::FloatingBits)
            throw std::logic_error("malformed fmov.4s immediate");
        float value = 0.0f;
        std::uint32_t bits = operands[1].floatingBits();
        std::memcpy(&value, &bits, sizeof(value));
        std::ostringstream immediate;
        immediate << std::setprecision(9) << value;
        output << "\tfmov " << registerName(operands[0]) << ".4s, #"
               << immediate.str() << '\n';
        break;
    }
    case Opcode::ADRP:
        output << "\tadrp " << registerName(operands[0]) << ", "
               << operands[1].symbol() << '\n';
        break;
    case Opcode::ADDlow:
        output << "\tadd " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", :lo12:"
               << operands[2].symbol() << '\n';
        break;
    case Opcode::ADDWrr: emitThreeRegisters(output, "add", instruction); break;
    case Opcode::SUBWrr: emitThreeRegisters(output, "sub", instruction); break;
    case Opcode::ADDWrs:
        output << "\tadd " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << registerName(operands[2]) << ", lsr #"
               << operands[3].immediate() << '\n';
        break;
    case Opcode::ADDWrsX:
        output << "\tadd "
               << registerNameAs(operands[0], RegClass::GPR32)
               << ", "
               << registerNameAs(operands[1], RegClass::GPR32)
               << ", "
               << registerNameAs(operands[2], RegClass::GPR32)
               << ", lsr #" << operands[3].immediate() << '\n';
        break;
    case Opcode::ADDWlsl:
        output << "\tadd " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << registerName(operands[2]) << ", lsl #"
               << operands[3].immediate() << '\n';
        break;
    case Opcode::NEGW:
        output << "\tneg " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::MULWrr: emitThreeRegisters(output, "mul", instruction); break;
    case Opcode::MULXrr: emitThreeRegisters(output, "mul", instruction); break;
    case Opcode::SDIVWrr: emitThreeRegisters(output, "sdiv", instruction); break;
    case Opcode::UDIVWrr: emitThreeRegisters(output, "udiv", instruction); break;
    case Opcode::UDIVXrr: emitThreeRegisters(output, "udiv", instruction); break;
    case Opcode::SMULLXrr:
        output << "\tsmull " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR32)
               << ", " << registerNameAs(operands[2], RegClass::GPR32)
               << '\n';
        break;
    case Opcode::SMADDLXrrr:
        output << "\tsmaddl "
               << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR32)
               << ", " << registerNameAs(operands[2], RegClass::GPR32)
               << ", " << registerNameAs(operands[3], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::SDIVXrr:
        output << "\tsdiv " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR64)
               << ", " << registerNameAs(operands[2], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::MSUBXrrr:
        output << "\tmsub " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR64)
               << ", " << registerNameAs(operands[2], RegClass::GPR64)
               << ", " << registerNameAs(operands[3], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::UMULHXrr:
        output << "\tumulh " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR64)
               << ", " << registerNameAs(operands[2], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::NEGX:
        output << "\tneg " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::ANDWrr: emitThreeRegisters(output, "and", instruction); break;
    case Opcode::ORRWrr: emitThreeRegisters(output, "orr", instruction); break;
    case Opcode::EORWrr: emitThreeRegisters(output, "eor", instruction); break;
    case Opcode::ANDXrr: emitThreeRegisters(output, "and", instruction); break;
    case Opcode::ORRXrr: emitThreeRegisters(output, "orr", instruction); break;
    case Opcode::EORXrr: emitThreeRegisters(output, "eor", instruction); break;
    case Opcode::LSLWrr: emitThreeRegisters(output, "lsl", instruction); break;
    case Opcode::LSRWrr: emitThreeRegisters(output, "lsr", instruction); break;
    case Opcode::ASRWrr: emitThreeRegisters(output, "asr", instruction); break;
    case Opcode::LSLXrr: emitThreeRegisters(output, "lsl", instruction); break;
    case Opcode::LSRXrr: emitThreeRegisters(output, "lsr", instruction); break;
    case Opcode::ASRXrr: emitThreeRegisters(output, "asr", instruction); break;
    case Opcode::ADDXrr: emitThreeRegisters(output, "add", instruction); break;
    case Opcode::SUBXrr: emitThreeRegisters(output, "sub", instruction); break;
    case Opcode::ADDWri: emitThreeWithImmediate(output, "add", instruction); break;
    case Opcode::SUBWri: emitThreeWithImmediate(output, "sub", instruction); break;
    case Opcode::ANDWri: emitThreeWithImmediate(output, "and", instruction); break;
    case Opcode::LSLWri: emitThreeWithImmediate(output, "lsl", instruction); break;
    case Opcode::LSRWri: emitThreeWithImmediate(output, "lsr", instruction); break;
    case Opcode::ASRWri: emitThreeWithImmediate(output, "asr", instruction); break;
    case Opcode::ADDXri: emitThreeWithImmediate(output, "add", instruction); break;
    case Opcode::SUBXri: emitThreeWithImmediate(output, "sub", instruction); break;
    case Opcode::LSLXri: emitThreeWithImmediate(output, "lsl", instruction); break;
    case Opcode::ASRXri: emitThreeWithImmediate(output, "asr", instruction); break;
    case Opcode::LSRXri: emitThreeWithImmediate(output, "lsr", instruction); break;
    case Opcode::COPYXtoW:
        if (!RegisterInfo::aliases(
                operands[0].physicalRegister(),
                operands[1].physicalRegister()))
            output << "\tmov "
                   << registerNameAs(operands[0], RegClass::GPR32)
                   << ", "
                   << registerNameAs(operands[1], RegClass::GPR32)
                   << '\n';
        break;
    case Opcode::SUBSPri: emitThreeWithImmediate(output, "sub", instruction); break;
    case Opcode::ADDSPri: emitThreeWithImmediate(output, "add", instruction); break;
    case Opcode::MOVXrr:
        output << "\tmov " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::ADDXrs:
        {
        std::int64_t extension = operands[4].immediate();
        output << "\tadd " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << registerName(operands[2]) << ", "
               << (extension == 2 ? "lsl"
                   : extension ? "sxtw" : "uxtw");
        if (operands[3].immediate())
            output << " #" << operands[3].immediate();
        output << '\n';
        break;
        }
    case Opcode::MADDWrrr:
    case Opcode::MSUBWrrr:
        output << '\t'
               << (instruction.opcode() == Opcode::MADDWrrr ? "madd" : "msub")
               << ' ' << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << registerName(operands[2]) << ", "
               << registerName(operands[3]) << '\n';
        break;
    case Opcode::CMPWrr:
        output << "\tcmp " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::CMPWri:
        output << "\tcmp " << registerName(operands[0]) << ", #"
               << operands[1].immediate() << '\n';
        break;
    case Opcode::CMPXrr:
        output << "\tcmp " << registerNameAs(operands[0], RegClass::GPR64)
               << ", " << registerNameAs(operands[1], RegClass::GPR64)
               << '\n';
        break;
    case Opcode::CMPXri:
        output << "\tcmp " << registerNameAs(operands[0], RegClass::GPR64)
               << ", #" << operands[1].immediate() << '\n';
        break;
    case Opcode::TSTWrr:
        output << "\ttst " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::TSTWri:
        output << "\ttst " << registerName(operands[0]) << ", #"
               << operands[1].immediate() << '\n';
        break;
    case Opcode::CSETW:
        output << "\tcset " << registerName(operands[0]) << ", "
               << conditionName(operands[1].condition()) << '\n';
        break;
    case Opcode::CNEGW:
        output << "\tcneg " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << conditionName(operands[2].condition()) << '\n';
        break;
    case Opcode::CSELW:
    case Opcode::CSELX:
    case Opcode::FCSELS:
        output << '\t'
               << (instruction.opcode() == Opcode::FCSELS ? "fcsel" : "csel")
               << ' ' << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", "
               << registerName(operands[2]) << ", "
               << conditionName(operands[3].condition()) << '\n';
        break;
    case Opcode::CLZW:
        output << "\tclz " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::RBITW:
        output << "\trbit " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::FADDS: emitThreeRegisters(output, "fadd", instruction); break;
    case Opcode::FSUBS: emitThreeRegisters(output, "fsub", instruction); break;
    case Opcode::FMULS: emitThreeRegisters(output, "fmul", instruction); break;
    case Opcode::FDIVS: emitThreeRegisters(output, "fdiv", instruction); break;
    case Opcode::FNEGS:
        output << "\tfneg " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::FCMPSrr:
        output << "\tfcmp " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::FCMPZS:
        output << "\tfcmp " << registerName(operands[0]) << ", #0.0\n";
        break;
    case Opcode::SCVTFWS:
        output << "\tscvtf " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::FCVTZSW:
        output << "\tfcvtzs " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::FMOVWS:
    case Opcode::FMOVSW:
        output << "\tfmov " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::SXTW:
    case Opcode::UXTW:
        output << '\t'
               << (instruction.opcode() == Opcode::SXTW ? "sxtw" : "uxtw")
               << ' ' << registerName(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
        break;
    case Opcode::LDRWui: case Opcode::LDRSui: case Opcode::LDRQui:
    case Opcode::LDRXui:
        emitMemory(output, "ldr", instruction);
        break;
    case Opcode::LDRWro: case Opcode::LDRSro: case Opcode::LDRQro:
    case Opcode::LDRXro:
        emitRegisterOffsetMemory(output, "ldr", instruction);
        break;
    case Opcode::LDRWlo: case Opcode::LDRSlo: case Opcode::LDRQlo:
    case Opcode::LDRXlo:
        emitGlobalMemory(output, "ldr", instruction);
        break;
    case Opcode::LDRDui:
        emitDMemory(output, "ldr", instruction);
        break;
    case Opcode::STRWui: case Opcode::STRSui: case Opcode::STRQui:
    case Opcode::STRXui:
        emitMemory(output, "str", instruction);
        break;
    case Opcode::STRWro: case Opcode::STRSro: case Opcode::STRQro:
    case Opcode::STRXro:
        emitRegisterOffsetMemory(output, "str", instruction);
        break;
    case Opcode::STRWlo: case Opcode::STRSlo: case Opcode::STRQlo:
    case Opcode::STRXlo:
        emitGlobalMemory(output, "str", instruction);
        break;
    case Opcode::STRDui:
        emitDMemory(output, "str", instruction);
        break;
    case Opcode::LDRWpost: case Opcode::LDRSpost:
    case Opcode::LDRQpost: case Opcode::LDRXpost:
        emitPostIndexedMemory(output, "ldr", instruction);
        break;
    case Opcode::STRWpost: case Opcode::STRSpost:
    case Opcode::STRQpost: case Opcode::STRXpost:
        emitPostIndexedMemory(output, "str", instruction);
        break;
    case Opcode::STPXpre:
        output << "\tstp " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", ["
               << registerName(operands[2]) << ", #"
               << operands[3].immediate() << "]!\n";
        break;
    case Opcode::LDPXpost:
        output << "\tldp " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ", ["
               << registerName(operands[2]) << "], #"
               << operands[3].immediate() << '\n';
        break;
    case Opcode::LDPWi: case Opcode::LDPSi:
    case Opcode::LDPXi: case Opcode::LDPDi: case Opcode::LDPQi:
    case Opcode::STPWi: case Opcode::STPSi:
    case Opcode::STPXi: case Opcode::STPDi: case Opcode::STPQi:
        {
        bool qPair =
            instruction.opcode() == Opcode::LDPQi ||
            instruction.opcode() == Opcode::STPQi;
        bool dPair =
            instruction.opcode() == Opcode::LDPDi ||
            instruction.opcode() == Opcode::STPDi;
        unsigned scale = qPair ? 16U : (dPair ||
            instruction.opcode() == Opcode::LDPXi ||
            instruction.opcode() == Opcode::STPXi) ? 8U : 4U;
        std::int64_t offset = operands[3].immediate();
        bool offsetEncodable =
            offset % scale == 0 && offset / scale >= -64 &&
            offset / scale <= 63;
        auto pairRegister = [&](const MachineOperand &operand) {
            std::string name = registerName(operand);
            return qPair ? "q" + name.substr(1)
                 : dPair ? "d" + name.substr(1) : name;
        };
        std::string base = registerName(operands[2]);
        if (!offsetEncodable)
            throw std::logic_error(
                "illegal pair offset reached assembly printer");
        bool isLoad =
            instruction.opcode() == Opcode::LDPWi ||
            instruction.opcode() == Opcode::LDPSi ||
            instruction.opcode() == Opcode::LDPXi ||
            instruction.opcode() == Opcode::LDPDi ||
            instruction.opcode() == Opcode::LDPQi;
        output << '\t'
               << (isLoad ? "ldp" : "stp")
               << ' ' << pairRegister(operands[0]) << ", "
               << pairRegister(operands[1]) << ", ["
               << base;
        if (offset)
            output << ", #" << offset;
        output << "]\n";
        break;
        }
    case Opcode::B:
        output << "\tb " << blockLabel(function, operands[0].basicBlock())
               << '\n';
        break;
    case Opcode::Bcc:
        output << "\tb." << conditionName(operands[0].condition()) << ' '
               << blockLabel(function, operands[1].basicBlock()) << '\n';
        break;
    case Opcode::CBZ:
    case Opcode::CBNZ:
        output << '\t'
               << (instruction.opcode() == Opcode::CBZ ? "cbz" : "cbnz")
               << ' ' << registerName(operands[0]) << ", "
               << blockLabel(function, operands[1].basicBlock()) << '\n';
        break;
    case Opcode::TBZ:
    case Opcode::TBNZ:
        output << '\t'
               << (instruction.opcode() == Opcode::TBZ ? "tbz" : "tbnz")
               << ' ' << registerName(operands[0]) << ", #"
               << operands[1].immediate() << ", "
               << blockLabel(function, operands[2].basicBlock()) << '\n';
        break;
    case Opcode::CALL:
        output << "\tbl " << operands[0].symbol() << '\n';
        break;
    case Opcode::TAILCALL:
        output << "\tb " << operands[0].symbol() << '\n';
        break;
    case Opcode::RET:
        output << "\tret\n";
        break;
    case Opcode::DUPv4i32:
    case Opcode::DUPv4f32:
        output << "\tdup " << vectorView(operands[0]) << ", "
               << (instruction.opcode() == Opcode::DUPv4f32
                       ? registerNameAs(operands[1], RegClass::NEON128) +
                             ".s[0]"
                       : registerName(operands[1]))
               << '\n';
        break;
    case Opcode::DUPv4sLane:
        if (operands.size() != 3 ||
            operands[2].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed dup lane");
        output << "\tdup " << vectorView(operands[0]) << ", "
               << registerName(operands[1]) << ".s["
               << operands[2].immediate() << "]\n";
        break;
    case Opcode::INSv4i32:
    case Opcode::INSv4f32:
        if (!RegisterInfo::aliases(
                operands[0].physicalRegister(),
                operands[1].physicalRegister()))
            throw std::logic_error(
                "unexpanded vector insert reached assembly printer");
        output << "\tmov " << registerName(operands[0]) << ".s["
               << operands[3].immediate() << "], ";
        if (instruction.opcode() == Opcode::INSv4f32)
            output << registerNameAs(
                          operands[2], RegClass::NEON128)
                   << ".s[0]\n";
        else
            output << registerName(operands[2]) << '\n';
        break;
    case Opcode::EXTRACTv4i32:
        output << "\tumov " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ".s["
               << operands[2].immediate() << "]\n";
        break;
    case Opcode::EXTRACTv4f32:
        output << "\tmov " << registerName(operands[0]) << ", "
               << registerName(operands[1]) << ".s["
               << operands[2].immediate() << "]\n";
        break;
    case Opcode::ADDv4i32: case Opcode::ADDv4f32:
    case Opcode::SUBv4i32: case Opcode::SUBv4f32:
    case Opcode::SMINv4i32: case Opcode::SMAXv4i32:
    case Opcode::MULv4i32: case Opcode::MULv4f32:
    case Opcode::DIVv4f32:
        output << '\t'
               << (instruction.opcode() == Opcode::ADDv4i32 ? "add"
                   : instruction.opcode() == Opcode::ADDv4f32 ? "fadd"
                   : instruction.opcode() == Opcode::SUBv4i32 ? "sub"
                   : instruction.opcode() == Opcode::SUBv4f32 ? "fsub"
                   : instruction.opcode() == Opcode::SMINv4i32 ? "smin"
                   : instruction.opcode() == Opcode::SMAXv4i32 ? "smax"
                   : instruction.opcode() == Opcode::MULv4i32 ? "mul"
                   : instruction.opcode() == Opcode::MULv4f32 ? "fmul"
                                                               : "fdiv")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << ", "
               << vectorView(operands[2]) << '\n';
        break;
    case Opcode::NEGv4i32:
    case Opcode::NEGv4f32:
        output << '\t'
               << (instruction.opcode() == Opcode::NEGv4i32
                       ? "neg" : "fneg")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << '\n';
        break;
    case Opcode::SSHLv4i32:
    case Opcode::USHLv4i32:
        output << '\t'
               << (instruction.opcode() == Opcode::SSHLv4i32
                       ? "sshl" : "ushl")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << ", "
               << vectorView(operands[2]) << '\n';
        break;
    case Opcode::SHLiv4i32:
    case Opcode::SSHRiv4i32:
    case Opcode::USHRiv4i32:
        output << '\t'
               << (instruction.opcode() == Opcode::SHLiv4i32 ? "shl"
                   : instruction.opcode() == Opcode::SSHRiv4i32 ? "sshr"
                                                                 : "ushr")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << ", #"
               << operands[2].immediate() << '\n';
        break;
    case Opcode::MLAv4i32: case Opcode::MLSv4i32:
    case Opcode::FMLAv4f32: case Opcode::FMLSv4f32:
        if (operands.size() != 4)
            throw std::logic_error("malformed vector multiply-accumulate");
        output << '\t'
               << (instruction.opcode() == Opcode::MLAv4i32 ? "mla"
                   : instruction.opcode() == Opcode::MLSv4i32 ? "mls"
                   : instruction.opcode() == Opcode::FMLAv4f32 ? "fmla"
                                                               : "fmls")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[2]) << ", "
               << vectorView(operands[3]) << '\n';
        break;
    case Opcode::SHUFFLEv16i8: {
        if (operands.size() != 4)
            throw std::logic_error("malformed vector shuffle");
        output << "\ttbl " << registerName(operands[0])
               << ".16b, {" << registerName(operands[1]) << ".16b, "
               << registerName(operands[2]) << ".16b}, "
               << registerName(operands[3]) << ".16b\n";
        break;
    }
    case Opcode::ZIP1v4s: case Opcode::ZIP2v4s:
    case Opcode::UZP1v4s: case Opcode::UZP2v4s:
    case Opcode::TRN1v4s: case Opcode::TRN2v4s:
        output << '\t'
               << (instruction.opcode() == Opcode::ZIP1v4s ? "zip1"
                   : instruction.opcode() == Opcode::ZIP2v4s ? "zip2"
                   : instruction.opcode() == Opcode::UZP1v4s ? "uzp1"
                   : instruction.opcode() == Opcode::UZP2v4s ? "uzp2"
                   : instruction.opcode() == Opcode::TRN1v4s ? "trn1"
                                                             : "trn2")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << ", "
               << vectorView(operands[2]) << '\n';
        break;
    case Opcode::EXTv16b:
        if (operands.size() != 4 ||
            operands[3].kind() != MachineOperand::Kind::Immediate)
            throw std::logic_error("malformed ext");
        output << "\text " << registerName(operands[0]) << ".16b, "
               << registerName(operands[1]) << ".16b, "
               << registerName(operands[2]) << ".16b, #"
               << operands[3].immediate() << '\n';
        break;
    case Opcode::REV64v4s:
        output << "\trev64 " << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << '\n';
        break;
    case Opcode::ADDVv4i32:
        output << "\taddv " << registerName(operands[0]) << ", "
               << vectorView(operands[1]) << '\n';
        break;
    case Opcode::ANDv16i8: case Opcode::ORRv16i8:
    case Opcode::EORv16i8:
        output << '\t'
               << (instruction.opcode() == Opcode::ANDv16i8 ? "and"
                   : instruction.opcode() == Opcode::ORRv16i8 ? "orr"
                                                              : "eor")
               << ' ' << registerName(operands[0]) << ".16b, "
               << registerName(operands[1]) << ".16b, "
               << registerName(operands[2]) << ".16b\n";
        break;
    case Opcode::PHI:
    case Opcode::MOVi32:
    case Opcode::MOVi64:
    case Opcode::LEA_FRAME:
    case Opcode::SPILL_LOAD:
    case Opcode::SPILL_STORE:
    case Opcode::FRAME_SETUP:
    case Opcode::FRAME_DESTROY:
    case Opcode::ADJCALLSTACKDOWN:
    case Opcode::ADJCALLSTACKUP:
    case Opcode::IMPLICIT_DEF:
    case Opcode::Invalid:
        throw std::logic_error(
            "unexpanded or unsupported opcode reached assembly printer");
    }
}

} // namespace

void AArch64AssemblyPrinter::printFunction(
    const MachineFunction &function, std::ostream &output) const {
    // The printer is an MC-style encoder only.  Integer immediates must
    // already be expanded into MOVZ/MOVK; the printer must never perform
    // instruction selection or invent a temporary register.
    if (!function.hasProperty(MachineProperty::Selected) ||
        !function.hasProperty(MachineProperty::NoVRegs) ||
        !function.hasProperty(MachineProperty::FrameFinalized))
        throw std::logic_error(
            "assembly printing requires selected, allocated, finalized MIR");
    output << "\t.p2align 2\n"
           << "\t.global " << function.name() << '\n'
           << "\t.type " << function.name() << ", %function\n"
           << function.name() << ":\n";
    for (const auto &block : function.blocks()) {
        if (block.get() != function.entryBlock())
            output << blockLabel(function, block.get()) << ":\n";
        for (const MachineInstr &instruction : block->instructions())
            printInstruction(function, instruction, output);
    }
    output << "\t.size " << function.name() << ", .-"
           << function.name() << "\n";
    const auto &pool = function.vectorConstantPool();
    if (!pool.empty()) {
        output << "\t.section .rodata\n";
        for (const MachineFunction::VectorConstantPoolEntry &entry : pool) {
            output << "\t.p2align 4\n" << entry.label << ":\n";
            for (std::uint32_t word : entry.lanes)
                output << "\t.word 0x" << std::hex << word << std::dec
                       << '\n';
        }
        output << "\t.text\n";
    }
}

std::string AArch64AssemblyPrinter::printFunction(
    const MachineFunction &function) const {
    std::ostringstream output;
    printFunction(function, output);
    return output.str();
}

} // namespace backend::aarch64
