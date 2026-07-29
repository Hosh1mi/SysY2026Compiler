#include "../../../include/backend/arm64/rewrite/asm_printer.hpp"

#include <array>
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

void emitIntegerConstant(std::ostream &output, const std::string &destination,
                         std::uint64_t value, unsigned width) {
    unsigned pieces = width / 16;
    unsigned first = pieces;
    for (unsigned i = 0; i < pieces; ++i)
        if (((value >> (i * 16)) & 0xffffU) != 0) {
            first = i;
            break;
        }
    if (first == pieces) {
        output << "\tmovz " << destination << ", #0\n";
        return;
    }
    output << "\tmovz " << destination << ", #"
           << ((value >> (first * 16)) & 0xffffU);
    if (first)
        output << ", lsl #" << first * 16;
    output << '\n';
    for (unsigned i = 0; i < pieces; ++i) {
        if (i == first)
            continue;
        std::uint64_t piece = (value >> (i * 16)) & 0xffffU;
        if (!piece)
            continue;
        output << "\tmovk " << destination << ", #" << piece;
        if (i)
            output << ", lsl #" << i * 16;
        output << '\n';
    }
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
    case Opcode::MOVi32:
        emitIntegerConstant(output, registerName(operands[0]),
                            static_cast<std::uint32_t>(
                                operands[1].immediate()), 32);
        break;
    case Opcode::MOVi64:
        emitIntegerConstant(output, registerName(operands[0]),
                            static_cast<std::uint64_t>(
                                operands[1].immediate()), 64);
        break;
    case Opcode::MOVIv4Zero:
        output << "\tmovi " << registerName(operands[0])
               << ".4s, #0\n";
        break;
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
    case Opcode::SDIVWrr: emitThreeRegisters(output, "sdiv", instruction); break;
    case Opcode::UDIVWrr: emitThreeRegisters(output, "udiv", instruction); break;
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
    case Opcode::ANDWrr: emitThreeRegisters(output, "and", instruction); break;
    case Opcode::ORRWrr: emitThreeRegisters(output, "orr", instruction); break;
    case Opcode::EORWrr: emitThreeRegisters(output, "eor", instruction); break;
    case Opcode::LSLWrr: emitThreeRegisters(output, "lsl", instruction); break;
    case Opcode::LSRWrr: emitThreeRegisters(output, "lsr", instruction); break;
    case Opcode::ASRWrr: emitThreeRegisters(output, "asr", instruction); break;
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
    case Opcode::LDPXi: case Opcode::LDPDi: case Opcode::LDPQi:
    case Opcode::STPXi: case Opcode::STPDi: case Opcode::STPQi:
        {
        bool qPair =
            instruction.opcode() == Opcode::LDPQi ||
            instruction.opcode() == Opcode::STPQi;
        bool dPair =
            instruction.opcode() == Opcode::LDPDi ||
            instruction.opcode() == Opcode::STPDi;
        std::int64_t offset = operands[3].immediate();
        unsigned scale = qPair ? 16U : 8U;
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
        output << '\t'
               << (instruction.opcode() == Opcode::LDPXi ||
                           instruction.opcode() == Opcode::LDPDi ||
                           instruction.opcode() == Opcode::LDPQi
                       ? "ldp" : "stp")
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
    case Opcode::RET:
        output << "\tret\n";
        break;
    case Opcode::DUPv4i32:
    case Opcode::DUPv4f32:
        output << "\tdup " << vectorView(operands[0]) << ", "
               << registerName(operands[1]) << '\n';
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
    case Opcode::MULv4i32: case Opcode::MULv4f32:
    case Opcode::DIVv4f32:
        output << '\t'
               << (instruction.opcode() == Opcode::ADDv4i32 ? "add"
                   : instruction.opcode() == Opcode::ADDv4f32 ? "fadd"
                   : instruction.opcode() == Opcode::SUBv4i32 ? "sub"
                   : instruction.opcode() == Opcode::SUBv4f32 ? "fsub"
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
    case Opcode::MLAv4i32: case Opcode::MLSv4i32:
    case Opcode::FMLAv4f32: case Opcode::FMLSv4f32:
        output << '\t'
               << (instruction.opcode() == Opcode::MLAv4i32 ? "mla"
                   : instruction.opcode() == Opcode::MLSv4i32 ? "mls"
                   : instruction.opcode() == Opcode::FMLAv4f32 ? "fmla"
                                                               : "fmls")
               << ' ' << vectorView(operands[0]) << ", "
               << vectorView(operands[1]) << ", "
               << vectorView(operands[2]) << '\n';
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
    case Opcode::LEA_FRAME:
    case Opcode::SPILL_LOAD:
    case Opcode::SPILL_STORE:
    case Opcode::FRAME_SETUP:
    case Opcode::FRAME_DESTROY:
    case Opcode::ADJCALLSTACKDOWN:
    case Opcode::ADJCALLSTACKUP:
    case Opcode::IMPLICIT_DEF:
    case Opcode::Invalid:
    case Opcode::LDRWro: case Opcode::STRWro:
    case Opcode::LDRSro: case Opcode::STRSro:
        throw std::logic_error(
            "unexpanded or unsupported opcode reached assembly printer");
    }
}

} // namespace

void AArch64AssemblyPrinter::printFunction(
    const MachineFunction &function, std::ostream &output) const {
    // The printer is an MC-style encoder only.  It may expand one explicit
    // destination constant pseudo into movz/movk pieces, but it must never
    // perform instruction selection or invent a temporary register.
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
}

std::string AArch64AssemblyPrinter::printFunction(
    const MachineFunction &function) const {
    std::ostringstream output;
    printFunction(function, output);
    return output.str();
}

} // namespace backend::aarch64
