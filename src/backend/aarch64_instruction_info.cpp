// This file defines generated AArch64 instruction metadata.
// Generated from aarch64.td by generate.py.
// Edit the target description or generator, not this file.
#include "backend/aarch64_instruction_info.hpp"

#include <array>
#include <cstddef>

namespace backend::aarch64::generated {
namespace {

constexpr std::size_t kOpcodeCount = static_cast<std::size_t>(Opcode::Count);

constexpr std::array<InstrDesc, kOpcodeCount> kDescriptors{{
    InstrDesc{}, // Invalid
    InstrDesc{Opcode::PHI, "PHI", 1, 0, false, false, false, false, false, false, false, true, false, false, 0, SchedResource::None},
    InstrDesc{Opcode::COPY, "COPY", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::IMPLICIT_DEF, "IMPLICIT_DEF", 1, 1, false, false, false, false, false, false, false, true, false, false, 0, SchedResource::None},
    InstrDesc{Opcode::ADJCALLSTACKDOWN, "ADJCALLSTACKDOWN", 0, 1, false, false, false, false, false, false, true, true, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADJCALLSTACKUP, "ADJCALLSTACKUP", 0, 1, false, false, false, false, false, false, true, true, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CALL, "bl", 0, 0, false, false, true, false, false, false, true, false, false, false, 3, SchedResource::Branch},
    InstrDesc{Opcode::TAILCALL, "b", 0, 0, true, true, true, true, false, false, true, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::RET, "ret", 0, 0, true, true, false, true, false, false, true, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::B, "b", 0, 1, true, true, false, false, false, false, false, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::Bcc, "b.cond", 0, 2, true, true, false, false, false, false, false, false, false, true, 1, SchedResource::Branch},
    InstrDesc{Opcode::CBZ, "cbz", 0, 2, true, true, false, false, false, false, false, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::CBNZ, "cbnz", 0, 2, true, true, false, false, false, false, false, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::TBZ, "tbz", 0, 3, true, true, false, false, false, false, false, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::TBNZ, "tbnz", 0, 3, true, true, false, false, false, false, false, false, false, false, 1, SchedResource::Branch},
    InstrDesc{Opcode::CSELW, "csel", 1, 4, false, false, false, false, false, false, false, false, false, true, 1, SchedResource::ALU},
    InstrDesc{Opcode::CSELX, "csel", 1, 4, false, false, false, false, false, false, false, false, false, true, 1, SchedResource::ALU},
    InstrDesc{Opcode::FCSELS, "fcsel", 1, 4, false, false, false, false, false, false, false, false, false, true, 2, SchedResource::FPALU},
    InstrDesc{Opcode::CSETW, "cset", 1, 2, false, false, false, false, false, false, false, false, false, true, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVi32, "MOVi32", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVi64, "MOVi64", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVZ, "movz", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVN, "movn", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVK, "movk", 1, 4, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::MOVIv4Zero, "movi", 1, 1, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::MOVIv4s, "movi", 1, 3, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::MOVIv4sMsl, "movi", 1, 3, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::MVNIv4s, "mvni", 1, 3, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::MOVIv16b, "movi", 1, 2, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::FMOVv4s, "fmov", 1, 2, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::ADRP, "adrp", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDlow, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LEA_FRAME, "LEA_FRAME", 1, 2, false, false, false, false, false, false, false, true, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDWrr, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDWri, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDWrs, "add", 1, 4, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDWrsX, "add", 1, 4, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDWlsl, "add", 1, 4, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SUBWrr, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SUBWri, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::NEGW, "neg", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CNEGW, "cneg", 1, 3, false, false, false, false, false, false, false, false, false, true, 1, SchedResource::ALU},
    InstrDesc{Opcode::MULWrr, "mul", 1, 3, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::MULXrr, "mul", 1, 3, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::MADDWrrr, "madd", 1, 4, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::MSUBWrrr, "msub", 1, 4, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::MSUBXrrr, "msub", 1, 4, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::SDIVWrr, "sdiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 12, SchedResource::Divide},
    InstrDesc{Opcode::UDIVWrr, "udiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 12, SchedResource::Divide},
    InstrDesc{Opcode::SDIVXrr, "sdiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 12, SchedResource::Divide},
    InstrDesc{Opcode::UDIVXrr, "udiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 12, SchedResource::Divide},
    InstrDesc{Opcode::SMULLXrr, "smull", 1, 3, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::SMADDLXrrr, "smaddl", 1, 4, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::UMULHXrr, "umulh", 1, 3, false, false, false, false, false, false, false, false, false, false, 3, SchedResource::MAC},
    InstrDesc{Opcode::NEGX, "neg", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CMPXrr, "cmp", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CMPXri, "cmp", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ANDWrr, "and", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ANDWri, "and", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ORRWrr, "orr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ORRWri, "orr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::EORWrr, "eor", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ANDXrr, "and", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ORRXrr, "orr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ORRXri, "orr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::EORXrr, "eor", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSLWrr, "lsl", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSLWri, "lsl", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSRWrr, "lsr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSRWri, "lsr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ASRWrr, "asr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ASRWri, "asr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSLXrr, "lsl", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSRXrr, "lsr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ASRXrr, "asr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSRXri, "lsr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CMPWrr, "cmp", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CMPWri, "cmp", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::TSTWrr, "tst", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::TSTWri, "tst", 0, 2, false, false, false, false, false, false, false, false, true, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::CLZW, "clz", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::RBITW, "rbit", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::FADDS, "fadd", 1, 3, false, false, false, false, false, false, false, false, false, false, 4, SchedResource::FPALU},
    InstrDesc{Opcode::FSUBS, "fsub", 1, 3, false, false, false, false, false, false, false, false, false, false, 4, SchedResource::FPALU},
    InstrDesc{Opcode::FMULS, "fmul", 1, 3, false, false, false, false, false, false, false, false, false, false, 4, SchedResource::FPMulDiv},
    InstrDesc{Opcode::FDIVS, "fdiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 18, SchedResource::FPMulDiv},
    InstrDesc{Opcode::FNEGS, "fneg", 1, 2, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::FCMPSrr, "fcmp", 0, 2, false, false, false, false, false, false, false, false, true, false, 3, SchedResource::FPALU},
    InstrDesc{Opcode::FCMPZS, "fcmp", 0, 1, false, false, false, false, false, false, false, false, true, false, 3, SchedResource::FPALU},
    InstrDesc{Opcode::SCVTFWS, "scvtf", 1, 2, false, false, false, false, false, false, false, false, false, false, 4, SchedResource::FPALU},
    InstrDesc{Opcode::FCVTZSW, "fcvtzs", 1, 2, false, false, false, false, false, false, false, false, false, false, 4, SchedResource::FPALU},
    InstrDesc{Opcode::FMOVWS, "fmov", 1, 2, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::FMOVSW, "fmov", 1, 2, false, false, false, false, false, false, false, false, false, false, 2, SchedResource::FPALU},
    InstrDesc{Opcode::LDRWui, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRWlo, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRWro, "ldr", 1, 5, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRWpost, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::STRWui, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRWlo, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRWro, "str", 0, 5, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRWpost, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRSui, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRSlo, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRSro, "ldr", 1, 5, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRSpost, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::STRSui, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRSlo, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRSro, "str", 0, 5, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRSpost, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRDui, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::STRDui, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRQui, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRQlo, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRQro, "ldr", 1, 5, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRQpost, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STRQui, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRQlo, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRQro, "str", 0, 5, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRQpost, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRXui, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRXlo, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRXro, "ldr", 1, 5, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::LDRXpost, "ldr", 1, 3, false, false, false, false, true, false, false, false, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::STRXui, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRXlo, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRXro, "str", 0, 5, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STRXpost, "str", 0, 3, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPWi, "ldp", 2, 4, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STPWi, "stp", 0, 4, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPSi, "ldp", 2, 4, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STPSi, "stp", 0, 4, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPXi, "ldp", 2, 4, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STPXi, "stp", 0, 4, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPDi, "ldp", 2, 4, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STPDi, "stp", 0, 4, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::STPXpre, "stp", 0, 4, false, false, false, false, false, true, true, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPXpost, "ldp", 3, 4, false, false, false, false, true, false, true, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::LDPQi, "ldp", 2, 4, false, false, false, false, true, false, false, false, false, false, 5, SchedResource::LoadStore},
    InstrDesc{Opcode::STPQi, "stp", 0, 4, false, false, false, false, false, true, false, false, false, false, 1, SchedResource::LoadStore},
    InstrDesc{Opcode::ADDXrr, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDXri, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDXrs, "add", 1, 5, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SUBXrr, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SUBXri, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::LSLXri, "lsl", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ASRXri, "asr", 1, 3, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::COPYXtoW, "mov", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::MOVXrr, "mov", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SUBSPri, "sub", 1, 3, false, false, false, false, false, false, true, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::ADDSPri, "add", 1, 3, false, false, false, false, false, false, true, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SXTW, "sxtw", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::UXTW, "uxtw", 1, 2, false, false, false, false, false, false, false, false, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::DUPv4i32, "dup", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::DUPv4f32, "dup", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::DUPv4sLane, "dup", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::INSv4i32, "ins", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::INSv4f32, "ins", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::EXTRACTv4i32, "umov", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::EXTRACTv4f32, "umov", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::ZIP1v4s, "zip1", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::ZIP2v4s, "zip2", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::UZP1v4s, "uzp1", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::UZP2v4s, "uzp2", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::TRN1v4s, "trn1", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::TRN2v4s, "trn2", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::EXTv16b, "ext", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::REV64v4s, "rev64", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::ADDv4i32, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SUBv4i32, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::MULv4i32, "mul", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv},
    InstrDesc{Opcode::SMINv4i32, "smin", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SMAXv4i32, "smax", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::NEGv4i32, "neg", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SSHLv4i32, "sshl", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::USHLv4i32, "ushl", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SHLiv4i32, "shl", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SSHRiv4i32, "sshr", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::USHRiv4i32, "ushr", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::MLAv4i32, "mla", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv, OperandConstraint{1, 0, 0}},
    InstrDesc{Opcode::MLSv4i32, "mls", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv, OperandConstraint{1, 0, 0}},
    InstrDesc{Opcode::ADDv4f32, "add", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SUBv4f32, "sub", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::MULv4f32, "fmul", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv},
    InstrDesc{Opcode::DIVv4f32, "fdiv", 1, 3, false, false, false, false, false, false, false, false, false, false, 18, SchedResource::FPMulDiv},
    InstrDesc{Opcode::NEGv4f32, "fneg", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::FMLAv4f32, "fmla", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::FMLSv4f32, "fmls", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPMulDiv, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::ANDv16i8, "and", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::ORRv16i8, "orr", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::EORv16i8, "eor", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::MVNv16i8, "mvn", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::CMEQv4i32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::CMGTv4i32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::CMGEv4i32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::CMHIv4i32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::CMHSv4i32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::FCMEQv4f32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::FCMGTv4f32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::FCMGEv4f32, "vector-compare", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::BSLv16i8, "bsl", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU, OperandConstraint{1, 0, -1}},
    InstrDesc{Opcode::TBL1v16i8, "tbl", 1, 3, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::SHUFFLEv16i8, "tbl", 1, 4, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::ADDVv4i32, "addv", 1, 2, false, false, false, false, false, false, false, false, false, false, 6, SchedResource::FPALU},
    InstrDesc{Opcode::FRAME_SETUP, "FRAME_SETUP", 0, 0, false, false, false, false, false, false, true, true, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::FRAME_DESTROY, "FRAME_DESTROY", 0, 0, false, false, false, false, false, false, true, true, false, false, 1, SchedResource::ALU},
    InstrDesc{Opcode::SPILL_LOAD, "SPILL_LOAD", 1, 2, false, false, false, false, true, false, false, true, false, false, 4, SchedResource::LoadStore},
    InstrDesc{Opcode::SPILL_STORE, "SPILL_STORE", 0, 2, false, false, false, false, false, true, false, true, false, false, 1, SchedResource::LoadStore},
}};

constexpr std::array<ImmediateConstraint, kOpcodeCount> kImmediateConstraints{{
    ImmediateConstraint::None, // Invalid
    ImmediateConstraint::None, // PHI
    ImmediateConstraint::None, // COPY
    ImmediateConstraint::None, // IMPLICIT_DEF
    ImmediateConstraint::None, // ADJCALLSTACKDOWN
    ImmediateConstraint::None, // ADJCALLSTACKUP
    ImmediateConstraint::None, // CALL
    ImmediateConstraint::None, // TAILCALL
    ImmediateConstraint::None, // RET
    ImmediateConstraint::None, // B
    ImmediateConstraint::None, // Bcc
    ImmediateConstraint::None, // CBZ
    ImmediateConstraint::None, // CBNZ
    ImmediateConstraint::None, // TBZ
    ImmediateConstraint::None, // TBNZ
    ImmediateConstraint::None, // CSELW
    ImmediateConstraint::None, // CSELX
    ImmediateConstraint::None, // FCSELS
    ImmediateConstraint::None, // CSETW
    ImmediateConstraint::None, // MOVi32
    ImmediateConstraint::None, // MOVi64
    ImmediateConstraint::None, // MOVZ
    ImmediateConstraint::None, // MOVN
    ImmediateConstraint::None, // MOVK
    ImmediateConstraint::None, // MOVIv4Zero
    ImmediateConstraint::None, // MOVIv4s
    ImmediateConstraint::None, // MOVIv4sMsl
    ImmediateConstraint::None, // MVNIv4s
    ImmediateConstraint::None, // MOVIv16b
    ImmediateConstraint::None, // FMOVv4s
    ImmediateConstraint::None, // ADRP
    ImmediateConstraint::None, // ADDlow
    ImmediateConstraint::None, // LEA_FRAME
    ImmediateConstraint::None, // ADDWrr
    ImmediateConstraint::AddSub12, // ADDWri
    ImmediateConstraint::None, // ADDWrs
    ImmediateConstraint::None, // ADDWrsX
    ImmediateConstraint::None, // ADDWlsl
    ImmediateConstraint::None, // SUBWrr
    ImmediateConstraint::AddSub12, // SUBWri
    ImmediateConstraint::None, // NEGW
    ImmediateConstraint::None, // CNEGW
    ImmediateConstraint::None, // MULWrr
    ImmediateConstraint::None, // MULXrr
    ImmediateConstraint::None, // MADDWrrr
    ImmediateConstraint::None, // MSUBWrrr
    ImmediateConstraint::None, // MSUBXrrr
    ImmediateConstraint::None, // SDIVWrr
    ImmediateConstraint::None, // UDIVWrr
    ImmediateConstraint::None, // SDIVXrr
    ImmediateConstraint::None, // UDIVXrr
    ImmediateConstraint::None, // SMULLXrr
    ImmediateConstraint::None, // SMADDLXrrr
    ImmediateConstraint::None, // UMULHXrr
    ImmediateConstraint::None, // NEGX
    ImmediateConstraint::None, // CMPXrr
    ImmediateConstraint::AddSub12, // CMPXri
    ImmediateConstraint::None, // ANDWrr
    ImmediateConstraint::None, // ANDWri
    ImmediateConstraint::None, // ORRWrr
    ImmediateConstraint::None, // ORRWri
    ImmediateConstraint::None, // EORWrr
    ImmediateConstraint::None, // ANDXrr
    ImmediateConstraint::None, // ORRXrr
    ImmediateConstraint::None, // ORRXri
    ImmediateConstraint::None, // EORXrr
    ImmediateConstraint::None, // LSLWrr
    ImmediateConstraint::Shift32, // LSLWri
    ImmediateConstraint::None, // LSRWrr
    ImmediateConstraint::Shift32, // LSRWri
    ImmediateConstraint::None, // ASRWrr
    ImmediateConstraint::Shift32, // ASRWri
    ImmediateConstraint::None, // LSLXrr
    ImmediateConstraint::None, // LSRXrr
    ImmediateConstraint::None, // ASRXrr
    ImmediateConstraint::Shift64, // LSRXri
    ImmediateConstraint::None, // CMPWrr
    ImmediateConstraint::AddSub12, // CMPWri
    ImmediateConstraint::None, // TSTWrr
    ImmediateConstraint::Logical32, // TSTWri
    ImmediateConstraint::None, // CLZW
    ImmediateConstraint::None, // RBITW
    ImmediateConstraint::None, // FADDS
    ImmediateConstraint::None, // FSUBS
    ImmediateConstraint::None, // FMULS
    ImmediateConstraint::None, // FDIVS
    ImmediateConstraint::None, // FNEGS
    ImmediateConstraint::None, // FCMPSrr
    ImmediateConstraint::None, // FCMPZS
    ImmediateConstraint::None, // SCVTFWS
    ImmediateConstraint::None, // FCVTZSW
    ImmediateConstraint::None, // FMOVWS
    ImmediateConstraint::None, // FMOVSW
    ImmediateConstraint::None, // LDRWui
    ImmediateConstraint::None, // LDRWlo
    ImmediateConstraint::None, // LDRWro
    ImmediateConstraint::None, // LDRWpost
    ImmediateConstraint::None, // STRWui
    ImmediateConstraint::None, // STRWlo
    ImmediateConstraint::None, // STRWro
    ImmediateConstraint::None, // STRWpost
    ImmediateConstraint::None, // LDRSui
    ImmediateConstraint::None, // LDRSlo
    ImmediateConstraint::None, // LDRSro
    ImmediateConstraint::None, // LDRSpost
    ImmediateConstraint::None, // STRSui
    ImmediateConstraint::None, // STRSlo
    ImmediateConstraint::None, // STRSro
    ImmediateConstraint::None, // STRSpost
    ImmediateConstraint::None, // LDRDui
    ImmediateConstraint::None, // STRDui
    ImmediateConstraint::None, // LDRQui
    ImmediateConstraint::None, // LDRQlo
    ImmediateConstraint::None, // LDRQro
    ImmediateConstraint::None, // LDRQpost
    ImmediateConstraint::None, // STRQui
    ImmediateConstraint::None, // STRQlo
    ImmediateConstraint::None, // STRQro
    ImmediateConstraint::None, // STRQpost
    ImmediateConstraint::None, // LDRXui
    ImmediateConstraint::None, // LDRXlo
    ImmediateConstraint::None, // LDRXro
    ImmediateConstraint::None, // LDRXpost
    ImmediateConstraint::None, // STRXui
    ImmediateConstraint::None, // STRXlo
    ImmediateConstraint::None, // STRXro
    ImmediateConstraint::None, // STRXpost
    ImmediateConstraint::None, // LDPWi
    ImmediateConstraint::None, // STPWi
    ImmediateConstraint::None, // LDPSi
    ImmediateConstraint::None, // STPSi
    ImmediateConstraint::None, // LDPXi
    ImmediateConstraint::None, // STPXi
    ImmediateConstraint::None, // LDPDi
    ImmediateConstraint::None, // STPDi
    ImmediateConstraint::None, // STPXpre
    ImmediateConstraint::None, // LDPXpost
    ImmediateConstraint::None, // LDPQi
    ImmediateConstraint::None, // STPQi
    ImmediateConstraint::None, // ADDXrr
    ImmediateConstraint::AddSub12, // ADDXri
    ImmediateConstraint::None, // ADDXrs
    ImmediateConstraint::None, // SUBXrr
    ImmediateConstraint::AddSub12, // SUBXri
    ImmediateConstraint::Shift64, // LSLXri
    ImmediateConstraint::Shift64, // ASRXri
    ImmediateConstraint::None, // COPYXtoW
    ImmediateConstraint::None, // MOVXrr
    ImmediateConstraint::AddSub12, // SUBSPri
    ImmediateConstraint::AddSub12, // ADDSPri
    ImmediateConstraint::None, // SXTW
    ImmediateConstraint::None, // UXTW
    ImmediateConstraint::None, // DUPv4i32
    ImmediateConstraint::None, // DUPv4f32
    ImmediateConstraint::None, // DUPv4sLane
    ImmediateConstraint::None, // INSv4i32
    ImmediateConstraint::None, // INSv4f32
    ImmediateConstraint::None, // EXTRACTv4i32
    ImmediateConstraint::None, // EXTRACTv4f32
    ImmediateConstraint::None, // ZIP1v4s
    ImmediateConstraint::None, // ZIP2v4s
    ImmediateConstraint::None, // UZP1v4s
    ImmediateConstraint::None, // UZP2v4s
    ImmediateConstraint::None, // TRN1v4s
    ImmediateConstraint::None, // TRN2v4s
    ImmediateConstraint::None, // EXTv16b
    ImmediateConstraint::None, // REV64v4s
    ImmediateConstraint::None, // ADDv4i32
    ImmediateConstraint::None, // SUBv4i32
    ImmediateConstraint::None, // MULv4i32
    ImmediateConstraint::None, // SMINv4i32
    ImmediateConstraint::None, // SMAXv4i32
    ImmediateConstraint::None, // NEGv4i32
    ImmediateConstraint::None, // SSHLv4i32
    ImmediateConstraint::None, // USHLv4i32
    ImmediateConstraint::None, // SHLiv4i32
    ImmediateConstraint::None, // SSHRiv4i32
    ImmediateConstraint::None, // USHRiv4i32
    ImmediateConstraint::None, // MLAv4i32
    ImmediateConstraint::None, // MLSv4i32
    ImmediateConstraint::None, // ADDv4f32
    ImmediateConstraint::None, // SUBv4f32
    ImmediateConstraint::None, // MULv4f32
    ImmediateConstraint::None, // DIVv4f32
    ImmediateConstraint::None, // NEGv4f32
    ImmediateConstraint::None, // FMLAv4f32
    ImmediateConstraint::None, // FMLSv4f32
    ImmediateConstraint::None, // ANDv16i8
    ImmediateConstraint::None, // ORRv16i8
    ImmediateConstraint::None, // EORv16i8
    ImmediateConstraint::None, // MVNv16i8
    ImmediateConstraint::None, // CMEQv4i32
    ImmediateConstraint::None, // CMGTv4i32
    ImmediateConstraint::None, // CMGEv4i32
    ImmediateConstraint::None, // CMHIv4i32
    ImmediateConstraint::None, // CMHSv4i32
    ImmediateConstraint::None, // FCMEQv4f32
    ImmediateConstraint::None, // FCMGTv4f32
    ImmediateConstraint::None, // FCMGEv4f32
    ImmediateConstraint::None, // BSLv16i8
    ImmediateConstraint::None, // TBL1v16i8
    ImmediateConstraint::None, // SHUFFLEv16i8
    ImmediateConstraint::None, // ADDVv4i32
    ImmediateConstraint::None, // FRAME_SETUP
    ImmediateConstraint::None, // FRAME_DESTROY
    ImmediateConstraint::None, // SPILL_LOAD
    ImmediateConstraint::None, // SPILL_STORE
}};

constexpr std::array<bool, kOpcodeCount> kCommutable{{
    false, // Invalid
    false, // PHI
    false, // COPY
    false, // IMPLICIT_DEF
    false, // ADJCALLSTACKDOWN
    false, // ADJCALLSTACKUP
    false, // CALL
    false, // TAILCALL
    false, // RET
    false, // B
    false, // Bcc
    false, // CBZ
    false, // CBNZ
    false, // TBZ
    false, // TBNZ
    false, // CSELW
    false, // CSELX
    false, // FCSELS
    false, // CSETW
    false, // MOVi32
    false, // MOVi64
    false, // MOVZ
    false, // MOVN
    false, // MOVK
    false, // MOVIv4Zero
    false, // MOVIv4s
    false, // MOVIv4sMsl
    false, // MVNIv4s
    false, // MOVIv16b
    false, // FMOVv4s
    false, // ADRP
    false, // ADDlow
    false, // LEA_FRAME
    true, // ADDWrr
    false, // ADDWri
    false, // ADDWrs
    false, // ADDWrsX
    false, // ADDWlsl
    false, // SUBWrr
    false, // SUBWri
    false, // NEGW
    false, // CNEGW
    true, // MULWrr
    true, // MULXrr
    false, // MADDWrrr
    false, // MSUBWrrr
    false, // MSUBXrrr
    false, // SDIVWrr
    false, // UDIVWrr
    false, // SDIVXrr
    false, // UDIVXrr
    false, // SMULLXrr
    false, // SMADDLXrrr
    false, // UMULHXrr
    false, // NEGX
    false, // CMPXrr
    false, // CMPXri
    true, // ANDWrr
    false, // ANDWri
    true, // ORRWrr
    false, // ORRWri
    true, // EORWrr
    true, // ANDXrr
    true, // ORRXrr
    false, // ORRXri
    true, // EORXrr
    false, // LSLWrr
    false, // LSLWri
    false, // LSRWrr
    false, // LSRWri
    false, // ASRWrr
    false, // ASRWri
    false, // LSLXrr
    false, // LSRXrr
    false, // ASRXrr
    false, // LSRXri
    false, // CMPWrr
    false, // CMPWri
    false, // TSTWrr
    false, // TSTWri
    false, // CLZW
    false, // RBITW
    true, // FADDS
    false, // FSUBS
    true, // FMULS
    false, // FDIVS
    false, // FNEGS
    false, // FCMPSrr
    false, // FCMPZS
    false, // SCVTFWS
    false, // FCVTZSW
    false, // FMOVWS
    false, // FMOVSW
    false, // LDRWui
    false, // LDRWlo
    false, // LDRWro
    false, // LDRWpost
    false, // STRWui
    false, // STRWlo
    false, // STRWro
    false, // STRWpost
    false, // LDRSui
    false, // LDRSlo
    false, // LDRSro
    false, // LDRSpost
    false, // STRSui
    false, // STRSlo
    false, // STRSro
    false, // STRSpost
    false, // LDRDui
    false, // STRDui
    false, // LDRQui
    false, // LDRQlo
    false, // LDRQro
    false, // LDRQpost
    false, // STRQui
    false, // STRQlo
    false, // STRQro
    false, // STRQpost
    false, // LDRXui
    false, // LDRXlo
    false, // LDRXro
    false, // LDRXpost
    false, // STRXui
    false, // STRXlo
    false, // STRXro
    false, // STRXpost
    false, // LDPWi
    false, // STPWi
    false, // LDPSi
    false, // STPSi
    false, // LDPXi
    false, // STPXi
    false, // LDPDi
    false, // STPDi
    false, // STPXpre
    false, // LDPXpost
    false, // LDPQi
    false, // STPQi
    true, // ADDXrr
    false, // ADDXri
    false, // ADDXrs
    false, // SUBXrr
    false, // SUBXri
    false, // LSLXri
    false, // ASRXri
    false, // COPYXtoW
    false, // MOVXrr
    false, // SUBSPri
    false, // ADDSPri
    false, // SXTW
    false, // UXTW
    false, // DUPv4i32
    false, // DUPv4f32
    false, // DUPv4sLane
    false, // INSv4i32
    false, // INSv4f32
    false, // EXTRACTv4i32
    false, // EXTRACTv4f32
    false, // ZIP1v4s
    false, // ZIP2v4s
    false, // UZP1v4s
    false, // UZP2v4s
    false, // TRN1v4s
    false, // TRN2v4s
    false, // EXTv16b
    false, // REV64v4s
    true, // ADDv4i32
    false, // SUBv4i32
    true, // MULv4i32
    true, // SMINv4i32
    true, // SMAXv4i32
    false, // NEGv4i32
    false, // SSHLv4i32
    false, // USHLv4i32
    false, // SHLiv4i32
    false, // SSHRiv4i32
    false, // USHRiv4i32
    false, // MLAv4i32
    false, // MLSv4i32
    true, // ADDv4f32
    false, // SUBv4f32
    true, // MULv4f32
    false, // DIVv4f32
    false, // NEGv4f32
    false, // FMLAv4f32
    false, // FMLSv4f32
    false, // ANDv16i8
    false, // ORRv16i8
    false, // EORv16i8
    false, // MVNv16i8
    false, // CMEQv4i32
    false, // CMGTv4i32
    false, // CMGEv4i32
    false, // CMHIv4i32
    false, // CMHSv4i32
    false, // FCMEQv4f32
    false, // FCMGTv4f32
    false, // FCMGEv4f32
    false, // BSLv16i8
    false, // TBL1v16i8
    false, // SHUFFLEv16i8
    false, // ADDVv4i32
    false, // FRAME_SETUP
    false, // FRAME_DESTROY
    false, // SPILL_LOAD
    false, // SPILL_STORE
}};

constexpr std::array<unsigned, kOpcodeCount> kRematerializationCosts{{
    0, // Invalid
    0, // PHI
    0, // COPY
    0, // IMPLICIT_DEF
    0, // ADJCALLSTACKDOWN
    0, // ADJCALLSTACKUP
    0, // CALL
    0, // TAILCALL
    0, // RET
    0, // B
    0, // Bcc
    0, // CBZ
    0, // CBNZ
    0, // TBZ
    0, // TBNZ
    0, // CSELW
    0, // CSELX
    0, // FCSELS
    0, // CSETW
    1, // MOVi32
    1, // MOVi64
    0, // MOVZ
    0, // MOVN
    0, // MOVK
    2, // MOVIv4Zero
    2, // MOVIv4s
    2, // MOVIv4sMsl
    2, // MVNIv4s
    2, // MOVIv16b
    2, // FMOVv4s
    1, // ADRP
    0, // ADDlow
    1, // LEA_FRAME
    0, // ADDWrr
    0, // ADDWri
    0, // ADDWrs
    0, // ADDWrsX
    0, // ADDWlsl
    0, // SUBWrr
    0, // SUBWri
    0, // NEGW
    0, // CNEGW
    0, // MULWrr
    0, // MULXrr
    0, // MADDWrrr
    0, // MSUBWrrr
    0, // MSUBXrrr
    0, // SDIVWrr
    0, // UDIVWrr
    0, // SDIVXrr
    0, // UDIVXrr
    0, // SMULLXrr
    0, // SMADDLXrrr
    0, // UMULHXrr
    0, // NEGX
    0, // CMPXrr
    0, // CMPXri
    0, // ANDWrr
    0, // ANDWri
    0, // ORRWrr
    0, // ORRWri
    0, // EORWrr
    0, // ANDXrr
    0, // ORRXrr
    0, // ORRXri
    0, // EORXrr
    0, // LSLWrr
    0, // LSLWri
    0, // LSRWrr
    0, // LSRWri
    0, // ASRWrr
    0, // ASRWri
    0, // LSLXrr
    0, // LSRXrr
    0, // ASRXrr
    0, // LSRXri
    0, // CMPWrr
    0, // CMPWri
    0, // TSTWrr
    0, // TSTWri
    0, // CLZW
    0, // RBITW
    0, // FADDS
    0, // FSUBS
    0, // FMULS
    0, // FDIVS
    0, // FNEGS
    0, // FCMPSrr
    0, // FCMPZS
    0, // SCVTFWS
    0, // FCVTZSW
    0, // FMOVWS
    0, // FMOVSW
    0, // LDRWui
    0, // LDRWlo
    0, // LDRWro
    0, // LDRWpost
    0, // STRWui
    0, // STRWlo
    0, // STRWro
    0, // STRWpost
    0, // LDRSui
    0, // LDRSlo
    0, // LDRSro
    0, // LDRSpost
    0, // STRSui
    0, // STRSlo
    0, // STRSro
    0, // STRSpost
    0, // LDRDui
    0, // STRDui
    0, // LDRQui
    0, // LDRQlo
    0, // LDRQro
    0, // LDRQpost
    0, // STRQui
    0, // STRQlo
    0, // STRQro
    0, // STRQpost
    0, // LDRXui
    0, // LDRXlo
    0, // LDRXro
    0, // LDRXpost
    0, // STRXui
    0, // STRXlo
    0, // STRXro
    0, // STRXpost
    0, // LDPWi
    0, // STPWi
    0, // LDPSi
    0, // STPSi
    0, // LDPXi
    0, // STPXi
    0, // LDPDi
    0, // STPDi
    0, // STPXpre
    0, // LDPXpost
    0, // LDPQi
    0, // STPQi
    0, // ADDXrr
    0, // ADDXri
    0, // ADDXrs
    0, // SUBXrr
    0, // SUBXri
    0, // LSLXri
    0, // ASRXri
    0, // COPYXtoW
    0, // MOVXrr
    0, // SUBSPri
    0, // ADDSPri
    0, // SXTW
    0, // UXTW
    0, // DUPv4i32
    0, // DUPv4f32
    0, // DUPv4sLane
    0, // INSv4i32
    0, // INSv4f32
    0, // EXTRACTv4i32
    0, // EXTRACTv4f32
    0, // ZIP1v4s
    0, // ZIP2v4s
    0, // UZP1v4s
    0, // UZP2v4s
    0, // TRN1v4s
    0, // TRN2v4s
    0, // EXTv16b
    0, // REV64v4s
    0, // ADDv4i32
    0, // SUBv4i32
    0, // MULv4i32
    0, // SMINv4i32
    0, // SMAXv4i32
    0, // NEGv4i32
    0, // SSHLv4i32
    0, // USHLv4i32
    0, // SHLiv4i32
    0, // SSHRiv4i32
    0, // USHRiv4i32
    0, // MLAv4i32
    0, // MLSv4i32
    0, // ADDv4f32
    0, // SUBv4f32
    0, // MULv4f32
    0, // DIVv4f32
    0, // NEGv4f32
    0, // FMLAv4f32
    0, // FMLSv4f32
    0, // ANDv16i8
    0, // ORRv16i8
    0, // EORv16i8
    0, // MVNv16i8
    0, // CMEQv4i32
    0, // CMGTv4i32
    0, // CMGEv4i32
    0, // CMHIv4i32
    0, // CMHSv4i32
    0, // FCMEQv4f32
    0, // FCMGTv4f32
    0, // FCMGEv4f32
    0, // BSLv16i8
    0, // TBL1v16i8
    0, // SHUFFLEv16i8
    0, // ADDVv4i32
    0, // FRAME_SETUP
    0, // FRAME_DESTROY
    0, // SPILL_LOAD
    0, // SPILL_STORE
}};

std::size_t opcodeIndex(Opcode opcode) {
    const auto index = static_cast<std::size_t>(opcode);
    return index < kOpcodeCount ? index : 0;
}

static_assert(kOpcodeCount == 207, "generated opcode table is incomplete");

} // namespace

const InstrDesc &instructionDescriptor(Opcode opcode) {
    return kDescriptors[opcodeIndex(opcode)];
}

ImmediateConstraint immediateConstraint(Opcode opcode) {
    return kImmediateConstraints[opcodeIndex(opcode)];
}

bool isCommutable(Opcode opcode) {
    return kCommutable[opcodeIndex(opcode)];
}

unsigned rematerializationCost(Opcode opcode) {
    return kRematerializationCosts[opcodeIndex(opcode)];
}

} // namespace backend::aarch64::generated
