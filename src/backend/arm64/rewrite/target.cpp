#include "../../../include/backend/arm64/rewrite/target.hpp"

#include <array>
#include <stdexcept>

namespace backend::aarch64 {
namespace {

constexpr unsigned regNumber(PhysReg reg) {
    return static_cast<unsigned>(reg);
}

bool isX(PhysReg reg) {
    return reg >= PhysReg::X0 && reg <= PhysReg::X30;
}

bool isV(PhysReg reg) {
    return reg >= PhysReg::V0 && reg <= PhysReg::V31;
}

const InstrDesc kInvalid{};

#define DESC(OP, MNEMONIC, DEFS, OPS, LATENCY, RESOURCE) \
    InstrDesc{Opcode::OP, MNEMONIC, DEFS, OPS, false, false, false, false, \
              false, false, false, false, false, false, LATENCY, RESOURCE}

const InstrDesc &descriptor(Opcode opcode) {
    switch (opcode) {
    case Opcode::PHI: {
        static const InstrDesc value{
            opcode, "PHI", 1, 0, false, false, false, false,
            false, false, false, true, false, false, 0,
            SchedResource::None};
        return value;
    }
    case Opcode::COPY: {
        static const InstrDesc value = DESC(COPY, "COPY", 1, 2, 1,
                                            SchedResource::ALU);
        return value;
    }
    case Opcode::IMPLICIT_DEF: {
        static const InstrDesc value{
            opcode, "IMPLICIT_DEF", 1, 1, false, false, false, false,
            false, false, false, true, false, false, 0,
            SchedResource::None};
        return value;
    }
    case Opcode::ADJCALLSTACKDOWN:
    case Opcode::ADJCALLSTACKUP: {
        static const InstrDesc down{
            Opcode::ADJCALLSTACKDOWN, "ADJCALLSTACKDOWN", 0, 1,
            false, false, false, false, false, false, true, true,
            false, false, 1, SchedResource::ALU};
        static const InstrDesc up{
            Opcode::ADJCALLSTACKUP, "ADJCALLSTACKUP", 0, 1,
            false, false, false, false, false, false, true, true,
            false, false, 1, SchedResource::ALU};
        return opcode == Opcode::ADJCALLSTACKDOWN ? down : up;
    }
    case Opcode::CALL: {
        static const InstrDesc value{
            opcode, "bl", 0, 0, false, false, true, false,
            false, false, true, false, false, false, 3,
            SchedResource::Branch};
        return value;
    }
    case Opcode::RET: {
        static const InstrDesc value{
            opcode, "ret", 0, 0, true, true, false, true,
            false, false, true, false, false, false, 1,
            SchedResource::Branch};
        return value;
    }
    case Opcode::B: {
        static const InstrDesc value{
            opcode, "b", 0, 1, true, true, false, false,
            false, false, false, false, false, false, 1,
            SchedResource::Branch};
        return value;
    }
    case Opcode::Bcc: {
        static const InstrDesc value{
            opcode, "b.cond", 0, 2, true, true, false, false,
            false, false, false, false, false, true, 1,
            SchedResource::Branch};
        return value;
    }
    case Opcode::CBZ:
    case Opcode::CBNZ: {
        static const InstrDesc cbz{
            Opcode::CBZ, "cbz", 0, 2, true, true, false, false,
            false, false, false, false, false, false, 1,
            SchedResource::Branch};
        static const InstrDesc cbnz = {
            Opcode::CBNZ, "cbnz", 0, 2, true, true, false, false,
            false, false, false, false, false, false, 1,
            SchedResource::Branch};
        return opcode == Opcode::CBZ ? cbz : cbnz;
    }
    case Opcode::TBZ:
    case Opcode::TBNZ: {
        static const InstrDesc tbz{
            Opcode::TBZ, "tbz", 0, 3, true, true, false, false,
            false, false, false, false, false, false, 1,
            SchedResource::Branch};
        static const InstrDesc tbnz{
            Opcode::TBNZ, "tbnz", 0, 3, true, true, false, false,
            false, false, false, false, false, false, 1,
            SchedResource::Branch};
        return opcode == Opcode::TBZ ? tbz : tbnz;
    }
    case Opcode::CSELW:
    case Opcode::CSELX:
    case Opcode::FCSELS: {
        static const InstrDesc w = DESC(CSELW, "csel", 1, 4, 1,
                                        SchedResource::ALU);
        static const InstrDesc x = DESC(CSELX, "csel", 1, 4, 1,
                                        SchedResource::ALU);
        static const InstrDesc s = DESC(FCSELS, "fcsel", 1, 4, 2,
                                        SchedResource::FPALU);
        return opcode == Opcode::CSELW ? w : opcode == Opcode::CSELX ? x : s;
    }
    case Opcode::CSETW: {
        static const InstrDesc value{
            Opcode::CSETW, "cset", 1, 2, false, false, false, false,
            false, false, false, false, false, true, 1,
            SchedResource::ALU};
        return value;
    }
    case Opcode::MOVi32:
    case Opcode::MOVi64:
    case Opcode::MOVIv4Zero: {
        static const InstrDesc w = DESC(MOVi32, "MOVi32", 1, 2, 1,
                                        SchedResource::ALU);
        static const InstrDesc x = DESC(MOVi64, "MOVi64", 1, 2, 1,
                                        SchedResource::ALU);
        static const InstrDesc v = DESC(MOVIv4Zero, "movi", 1, 1, 2,
                                        SchedResource::FPALU);
        return opcode == Opcode::MOVi32 ? w
             : opcode == Opcode::MOVi64 ? x : v;
    }
    case Opcode::ADRP: {
        static const InstrDesc value = DESC(ADRP, "adrp", 1, 2, 1,
                                            SchedResource::ALU);
        return value;
    }
    case Opcode::ADDlow: {
        static const InstrDesc value = DESC(ADDlow, "add", 1, 3, 1,
                                            SchedResource::ALU);
        return value;
    }
    case Opcode::LEA_FRAME: {
        static const InstrDesc value{
            Opcode::LEA_FRAME, "LEA_FRAME", 1, 2, false, false, false, false,
            false, false, false, true, false, false, 1,
            SchedResource::ALU};
        return value;
    }
#define SIMPLE_CASE(OP, MN, DEFS, OPS, LAT, RES) \
    case Opcode::OP: { \
        static const InstrDesc value = DESC(OP, MN, DEFS, OPS, LAT, RES); \
        return value; \
    }
    SIMPLE_CASE(ADDWrr, "add", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ADDWri, "add", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ADDWrs, "add", 1, 4, 1, SchedResource::ALU)
    SIMPLE_CASE(ADDWrsX, "add", 1, 4, 1, SchedResource::ALU)
    SIMPLE_CASE(ADDWlsl, "add", 1, 4, 1, SchedResource::ALU)
    SIMPLE_CASE(SUBWrr, "sub", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(SUBWri, "sub", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(NEGW, "neg", 1, 2, 1, SchedResource::ALU)
    case Opcode::CNEGW: {
        static const InstrDesc value{
            Opcode::CNEGW, "cneg", 1, 3, false, false, false, false,
            false, false, false, false, false, true, 1,
            SchedResource::ALU};
        return value;
    }
    SIMPLE_CASE(MULWrr, "mul", 1, 3, 3, SchedResource::MAC)
    SIMPLE_CASE(MADDWrrr, "madd", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(MSUBWrrr, "msub", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(SDIVWrr, "sdiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(UDIVWrr, "udiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(SMULLXrr, "smull", 1, 3, 3, SchedResource::MAC)
    SIMPLE_CASE(SMADDLXrrr, "smaddl", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(ANDWrr, "and", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ANDWri, "and", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ORRWrr, "orr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(EORWrr, "eor", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSLWrr, "lsl", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSLWri, "lsl", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRWrr, "lsr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRWri, "lsr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ASRWrr, "asr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ASRWri, "asr", 1, 3, 1, SchedResource::ALU)
    case Opcode::CMPWrr:
    case Opcode::CMPWri:
    case Opcode::TSTWrr: {
        static const InstrDesc cmp{
            Opcode::CMPWrr, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc cmpi{
            Opcode::CMPWri, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc tst{
            Opcode::TSTWrr, "tst", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        return opcode == Opcode::CMPWrr ? cmp
             : opcode == Opcode::CMPWri ? cmpi : tst;
    }
    SIMPLE_CASE(CLZW, "clz", 1, 2, 1, SchedResource::ALU)
    SIMPLE_CASE(RBITW, "rbit", 1, 2, 1, SchedResource::ALU)
    SIMPLE_CASE(FADDS, "fadd", 1, 3, 4, SchedResource::FPALU)
    SIMPLE_CASE(FSUBS, "fsub", 1, 3, 4, SchedResource::FPALU)
    SIMPLE_CASE(FMULS, "fmul", 1, 3, 4, SchedResource::FPMulDiv)
    SIMPLE_CASE(FDIVS, "fdiv", 1, 3, 18, SchedResource::FPMulDiv)
    SIMPLE_CASE(FNEGS, "fneg", 1, 2, 2, SchedResource::FPALU)
    case Opcode::FCMPSrr:
    case Opcode::FCMPZS: {
        static const InstrDesc rr{
            Opcode::FCMPSrr, "fcmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 3,
            SchedResource::FPALU};
        static const InstrDesc zero{
            Opcode::FCMPZS, "fcmp", 0, 1, false, false, false, false,
            false, false, false, false, true, false, 3,
            SchedResource::FPALU};
        return opcode == Opcode::FCMPSrr ? rr : zero;
    }
    SIMPLE_CASE(SCVTFWS, "scvtf", 1, 2, 4, SchedResource::FPALU)
    SIMPLE_CASE(FCVTZSW, "fcvtzs", 1, 2, 4, SchedResource::FPALU)
    SIMPLE_CASE(FMOVWS, "fmov", 1, 2, 2, SchedResource::FPALU)
    SIMPLE_CASE(FMOVSW, "fmov", 1, 2, 2, SchedResource::FPALU)
#undef SIMPLE_CASE
    default:
        break;
    }

    // Loads, stores, address arithmetic, and vector instructions share a
    // compact fallback descriptor construction.  They remain fully typed by
    // opcode; only common scheduling properties are grouped here.
    static thread_local InstrDesc dynamic;
    dynamic = {};
    dynamic.opcode = opcode;
    dynamic.latency = 1;
    dynamic.resource = SchedResource::ALU;
    switch (opcode) {
    case Opcode::LDRWui: case Opcode::LDRWlo: case Opcode::LDRWro:
    case Opcode::LDRWpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRSui: case Opcode::LDRSlo: case Opcode::LDRSro:
    case Opcode::LDRSpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRDui:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRQui: case Opcode::LDRQlo: case Opcode::LDRQpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 5; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRXui: case Opcode::LDRXlo: case Opcode::LDRXpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::STRWui: case Opcode::STRWlo: case Opcode::STRWro:
    case Opcode::STRWpost:
    case Opcode::STRSui: case Opcode::STRSlo: case Opcode::STRSro:
    case Opcode::STRSpost:
    case Opcode::STRDui:
    case Opcode::STRQui: case Opcode::STRQlo: case Opcode::STRQpost:
    case Opcode::STRXui: case Opcode::STRXlo: case Opcode::STRXpost:
        dynamic.mnemonic = "str"; dynamic.explicitOperands = 3;
        dynamic.mayStore = true; dynamic.resource = SchedResource::LoadStore;
        break;
    case Opcode::LDPXi: case Opcode::LDPDi: case Opcode::LDPQi:
        dynamic.mnemonic = "ldp"; dynamic.explicitDefs = 2;
        dynamic.explicitOperands = 4; dynamic.mayLoad = true;
        dynamic.latency = 5; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::STPXi: case Opcode::STPDi: case Opcode::STPQi:
        dynamic.mnemonic = "stp"; dynamic.explicitOperands = 4;
        dynamic.mayStore = true; dynamic.resource = SchedResource::LoadStore;
        break;
    case Opcode::STPXpre:
        dynamic.mnemonic = "stp"; dynamic.explicitOperands = 4;
        dynamic.mayStore = true; dynamic.hasSideEffects = true;
        dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDPXpost:
        dynamic.mnemonic = "ldp"; dynamic.explicitDefs = 3;
        dynamic.explicitOperands = 4; dynamic.mayLoad = true;
        dynamic.hasSideEffects = true; dynamic.latency = 5;
        dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::ADDXrr: case Opcode::ADDXri:
        dynamic.mnemonic = "add"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; break;
    case Opcode::ADDXrs:
        dynamic.mnemonic = "add"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 5; break;
    case Opcode::SUBXrr: case Opcode::SUBXri:
        dynamic.mnemonic = "sub"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; break;
    case Opcode::LSLXri:
        dynamic.mnemonic = "lsl"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; break;
    case Opcode::ASRXri:
        dynamic.mnemonic = "asr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; break;
    case Opcode::COPYXtoW:
        dynamic.mnemonic = "mov"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; break;
    case Opcode::MOVXrr:
        dynamic.mnemonic = "mov"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; break;
    case Opcode::SUBSPri:
        dynamic.mnemonic = "sub"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.hasSideEffects = true; break;
    case Opcode::ADDSPri:
        dynamic.mnemonic = "add"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.hasSideEffects = true; break;
    case Opcode::SXTW:
        dynamic.mnemonic = "sxtw"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; break;
    case Opcode::UXTW:
        dynamic.mnemonic = "uxtw"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; break;
    case Opcode::DUPv4i32: case Opcode::DUPv4f32:
        dynamic.mnemonic = "dup"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::INSv4i32: case Opcode::INSv4f32:
        dynamic.mnemonic = "ins"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::EXTRACTv4i32: case Opcode::EXTRACTv4f32:
        dynamic.mnemonic = "umov"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::ADDv4i32: case Opcode::SUBv4i32:
    case Opcode::SMINv4i32: case Opcode::SMAXv4i32:
    case Opcode::ADDv4f32: case Opcode::SUBv4f32:
    case Opcode::ANDv16i8: case Opcode::ORRv16i8: case Opcode::EORv16i8:
        dynamic.mnemonic = opcode == Opcode::ADDv4i32 || opcode == Opcode::ADDv4f32 ? "add"
                           : opcode == Opcode::SUBv4i32 || opcode == Opcode::SUBv4f32 ? "sub"
                           : opcode == Opcode::SMINv4i32 ? "smin"
                           : opcode == Opcode::SMAXv4i32 ? "smax"
                           : opcode == Opcode::ANDv16i8 ? "and"
                           : opcode == Opcode::ORRv16i8 ? "orr" : "eor";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 3; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::MULv4i32: case Opcode::MLAv4i32: case Opcode::MLSv4i32:
    case Opcode::MULv4f32: case Opcode::DIVv4f32:
    case Opcode::FMLAv4f32: case Opcode::FMLSv4f32:
        dynamic.mnemonic = opcode == Opcode::MULv4i32 ? "mul"
                           : opcode == Opcode::MLAv4i32 ? "mla"
                           : opcode == Opcode::MLSv4i32 ? "mls"
                           : opcode == Opcode::MULv4f32 ? "fmul"
                           : opcode == Opcode::DIVv4f32 ? "fdiv"
                           : opcode == Opcode::FMLAv4f32 ? "fmla" : "fmls";
        dynamic.explicitDefs = 1;
        dynamic.explicitOperands =
            opcode == Opcode::MULv4i32 ||
                    opcode == Opcode::MULv4f32 ||
                    opcode == Opcode::DIVv4f32 ? 3 : 4;
        dynamic.latency = 5; dynamic.resource = SchedResource::FPMulDiv; break;
    case Opcode::NEGv4f32:
        dynamic.mnemonic = "fneg"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 2;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::NEGv4i32:
        dynamic.mnemonic = "neg"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 2;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SSHLv4i32: case Opcode::USHLv4i32:
        dynamic.mnemonic =
            opcode == Opcode::SSHLv4i32 ? "sshl" : "ushl";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 3; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SHUFFLEv16i8:
        dynamic.mnemonic = "tbl"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.latency = 4;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::ADDVv4i32:
        dynamic.mnemonic = "addv"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 4;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::FRAME_SETUP:
        dynamic.mnemonic = "FRAME_SETUP"; dynamic.pseudo = true;
        dynamic.hasSideEffects = true; break;
    case Opcode::FRAME_DESTROY:
        dynamic.mnemonic = "FRAME_DESTROY"; dynamic.pseudo = true;
        dynamic.hasSideEffects = true; break;
    case Opcode::SPILL_LOAD:
        dynamic.mnemonic = "SPILL_LOAD"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.mayLoad = true;
        dynamic.pseudo = true; dynamic.latency = 4;
        dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::SPILL_STORE:
        dynamic.mnemonic = "SPILL_STORE"; dynamic.explicitOperands = 2;
        dynamic.mayStore = true; dynamic.pseudo = true;
        dynamic.resource = SchedResource::LoadStore; break;
    default:
        return kInvalid;
    }
    return dynamic;
}

#undef DESC

} // namespace

RegClass RegisterInfo::classForType(ValueType type) {
    switch (type) {
    case ValueType::I1:
    case ValueType::I32:
        return RegClass::GPR32;
    case ValueType::Ptr:
        return RegClass::GPR64;
    case ValueType::F32:
        return RegClass::FPR32;
    case ValueType::V4I32:
    case ValueType::V4F32:
        return RegClass::NEON128;
    case ValueType::Flags:
        return RegClass::CCR;
    default:
        return RegClass::Invalid;
    }
}

bool RegisterInfo::isGPR(PhysReg reg) {
    return isX(reg) || reg == PhysReg::SP || reg == PhysReg::XZR;
}

bool RegisterInfo::isVector(PhysReg reg) {
    return isV(reg);
}

bool RegisterInfo::aliases(PhysReg lhs, PhysReg rhs) {
    return lhs != PhysReg::NoReg && lhs == rhs;
}

bool RegisterInfo::isReserved(PhysReg reg) {
    return reg == PhysReg::NoReg || reg == PhysReg::SP ||
           reg == PhysReg::XZR || reg == PhysReg::X18 ||
           reg == PhysReg::X29 || reg == PhysReg::X30 ||
           reg == PhysReg::NZCV;
}

bool RegisterInfo::isCallerSaved(PhysReg reg) {
    if (reg >= PhysReg::X0 && reg <= PhysReg::X17)
        return reg != PhysReg::X18;
    return reg >= PhysReg::V0 && reg <= PhysReg::V7 ||
           reg >= PhysReg::V16 && reg <= PhysReg::V31;
}

bool RegisterInfo::isCalleeSaved(PhysReg reg) {
    return reg >= PhysReg::X19 && reg <= PhysReg::X28 ||
           reg >= PhysReg::V8 && reg <= PhysReg::V15;
}

std::string_view RegisterInfo::name(PhysReg reg, RegClass view) {
    static const std::array<std::string_view, 31> xNames = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30"};
    static const std::array<std::string_view, 31> wNames = {
        "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7",
        "w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15",
        "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
        "w24", "w25", "w26", "w27", "w28", "w29", "w30"};
    static const std::array<std::string_view, 32> vNames = {
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"};
    static const std::array<std::string_view, 32> sNames = {
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15",
        "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
        "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31"};

    if (isX(reg)) {
        unsigned index = regNumber(reg) - regNumber(PhysReg::X0);
        return view == RegClass::GPR32 ? wNames[index] : xNames[index];
    }
    if (isV(reg)) {
        unsigned index = regNumber(reg) - regNumber(PhysReg::V0);
        return view == RegClass::FPR32 ? sNames[index] : vNames[index];
    }
    if (reg == PhysReg::SP)
        return view == RegClass::GPR32 ? "wsp" : "sp";
    if (reg == PhysReg::XZR)
        return view == RegClass::GPR32 ? "wzr" : "xzr";
    if (reg == PhysReg::NZCV)
        return "nzcv";
    return "<noreg>";
}

const std::vector<PhysReg> &RegisterInfo::allocationOrder(RegClass regClass) {
    static const std::vector<PhysReg> gprs = {
        PhysReg::X9, PhysReg::X10, PhysReg::X11, PhysReg::X12,
        PhysReg::X13, PhysReg::X14, PhysReg::X15,
        PhysReg::X16, PhysReg::X17,
        PhysReg::X19, PhysReg::X20, PhysReg::X21, PhysReg::X22,
        PhysReg::X23, PhysReg::X24, PhysReg::X25, PhysReg::X26,
        PhysReg::X27, PhysReg::X28,
        PhysReg::X8, PhysReg::X7, PhysReg::X6, PhysReg::X5,
        PhysReg::X4, PhysReg::X3, PhysReg::X2, PhysReg::X1, PhysReg::X0};
    static const std::vector<PhysReg> vectors = {
        PhysReg::V16, PhysReg::V17, PhysReg::V18, PhysReg::V19,
        PhysReg::V20, PhysReg::V21, PhysReg::V22, PhysReg::V23,
        PhysReg::V24, PhysReg::V25, PhysReg::V26, PhysReg::V27,
        PhysReg::V28, PhysReg::V29, PhysReg::V30, PhysReg::V31,
        PhysReg::V8, PhysReg::V9, PhysReg::V10, PhysReg::V11,
        PhysReg::V12, PhysReg::V13, PhysReg::V14, PhysReg::V15,
        PhysReg::V7, PhysReg::V6, PhysReg::V5, PhysReg::V4,
        PhysReg::V3, PhysReg::V2, PhysReg::V1, PhysReg::V0};
    static const std::vector<PhysReg> none;
    if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
        return gprs;
    if (regClass == RegClass::FPR32 || regClass == RegClass::NEON128)
        return vectors;
    return none;
}

const std::vector<PhysReg> &RegisterInfo::calleeSaved(RegClass regClass) {
    static const std::vector<PhysReg> gprs = {
        PhysReg::X19, PhysReg::X20, PhysReg::X21, PhysReg::X22,
        PhysReg::X23, PhysReg::X24, PhysReg::X25, PhysReg::X26,
        PhysReg::X27, PhysReg::X28};
    static const std::vector<PhysReg> vectors = {
        PhysReg::V8, PhysReg::V9, PhysReg::V10, PhysReg::V11,
        PhysReg::V12, PhysReg::V13, PhysReg::V14, PhysReg::V15};
    static const std::vector<PhysReg> none;
    if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
        return gprs;
    if (regClass == RegClass::FPR32 || regClass == RegClass::NEON128)
        return vectors;
    return none;
}

const InstrDesc &InstrInfo::get(Opcode opcode) {
    return descriptor(opcode);
}

bool InstrInfo::acceptsImmediate(Opcode opcode, std::int64_t immediate) {
    switch (opcode) {
    case Opcode::ADDWri:
    case Opcode::SUBWri:
    case Opcode::ADDXri:
    case Opcode::SUBXri:
    case Opcode::CMPWri:
    case Opcode::SUBSPri:
    case Opcode::ADDSPri:
        return immediate >= 0 && immediate <= 4095;
    case Opcode::LSLXri:
        return immediate >= 0 && immediate <= 63;
    case Opcode::LSLWri:
    case Opcode::LSRWri:
    case Opcode::ASRWri:
        return immediate >= 0 && immediate <= 31;
    default:
        return false;
    }
}

bool InstrInfo::isCommutable(Opcode opcode) {
    switch (opcode) {
    case Opcode::ADDWrr:
    case Opcode::MULWrr:
    case Opcode::ANDWrr:
    case Opcode::ORRWrr:
    case Opcode::EORWrr:
    case Opcode::FADDS:
    case Opcode::FMULS:
    case Opcode::ADDv4i32:
    case Opcode::MULv4i32:
    case Opcode::SMINv4i32:
    case Opcode::SMAXv4i32:
    case Opcode::ADDv4f32:
    case Opcode::MULv4f32:
        return true;
    default:
        return false;
    }
}

} // namespace backend::aarch64
