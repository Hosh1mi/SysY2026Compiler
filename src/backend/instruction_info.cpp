// This file owns opcode semantics, immediate constraints, condition handling,
// and scheduling metadata shared by all machine-code stages.
#include "backend/target.hpp"

namespace backend::aarch64 {
namespace {

bool isLogicalImmediate32(std::uint32_t value) {
    if (value == 0 || value == UINT32_MAX)
        return false;

    for (unsigned elementBits = 2; elementBits <= 32;
         elementBits *= 2) {
        std::uint32_t elementMask =
            elementBits == 32
                ? UINT32_MAX
                : (std::uint32_t{1} << elementBits) - 1;
        std::uint32_t element = value & elementMask;
        std::uint32_t replicated = 0;
        for (unsigned offset = 0; offset < 32;
             offset += elementBits)
            replicated |= element << offset;
        if (replicated != value)
            continue;

        for (unsigned ones = 1; ones < elementBits; ++ones) {
            std::uint32_t run =
                (std::uint32_t{1} << ones) - 1;
            for (unsigned rotate = 0; rotate < elementBits;
                 ++rotate) {
                std::uint32_t rotated =
                    rotate == 0
                        ? run
                        : ((run >> rotate) |
                           (run << (elementBits - rotate))) &
                              elementMask;
                if (rotated == element)
                    return true;
            }
        }
    }
    return false;
}

const InstrDesc kInvalid{};

#define DESC(OP, MNEMONIC, DEFS, OPS, LATENCY, RESOURCE) \
    InstrDesc{Opcode::OP, MNEMONIC, DEFS, OPS, false, false, false, false, \
              false, false, false, false, false, false, LATENCY, RESOURCE}

InstrDesc descriptor(Opcode opcode) {
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
    case Opcode::TAILCALL: {
        // Sibling/general TCO: tear down the frame then branch.  Treated as
        // both a call (uses arg regs) and a return terminator (needs epilogue).
        static const InstrDesc value{
            opcode, "b", 0, 0, true, true, true, true,
            false, false, true, false, false, false, 1,
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
        static const InstrDesc w{
            Opcode::CSELW, "csel", 1, 4, false, false, false, false,
            false, false, false, false, false, true, 1,
            SchedResource::ALU};
        static const InstrDesc x{
            Opcode::CSELX, "csel", 1, 4, false, false, false, false,
            false, false, false, false, false, true, 1,
            SchedResource::ALU};
        static const InstrDesc s{
            Opcode::FCSELS, "fcsel", 1, 4, false, false, false, false,
            false, false, false, false, false, true, 2,
            SchedResource::FPALU};
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
    case Opcode::MOVZ:
    case Opcode::MOVN:
    case Opcode::MOVK:
    case Opcode::MOVIv4Zero:
    case Opcode::MOVIv4s:
    case Opcode::MOVIv4sMsl:
    case Opcode::MVNIv4s:
    case Opcode::MOVIv16b:
    case Opcode::FMOVv4s: {
        static const InstrDesc w = DESC(MOVi32, "MOVi32", 1, 2, 1,
                                        SchedResource::ALU);
        static const InstrDesc x = DESC(MOVi64, "MOVi64", 1, 2, 1,
                                        SchedResource::ALU);
        static const InstrDesc movz = DESC(MOVZ, "movz", 1, 3, 1,
                                           SchedResource::ALU);
        static const InstrDesc movn = DESC(MOVN, "movn", 1, 3, 1,
                                           SchedResource::ALU);
        static const InstrDesc movk = DESC(MOVK, "movk", 1, 4, 1,
                                           SchedResource::ALU);
        static const InstrDesc v = DESC(MOVIv4Zero, "movi", 1, 1, 2,
                                        SchedResource::FPALU);
        static const InstrDesc movi4s = DESC(MOVIv4s, "movi", 1, 3, 2,
                                             SchedResource::FPALU);
        static const InstrDesc movimsl = DESC(MOVIv4sMsl, "movi", 1, 3, 2,
                                              SchedResource::FPALU);
        static const InstrDesc mvni4s = DESC(MVNIv4s, "mvni", 1, 3, 2,
                                             SchedResource::FPALU);
        static const InstrDesc movi16b = DESC(MOVIv16b, "movi", 1, 2, 2,
                                              SchedResource::FPALU);
        static const InstrDesc fmov4s = DESC(FMOVv4s, "fmov", 1, 2, 2,
                                             SchedResource::FPALU);
        return opcode == Opcode::MOVi32 ? w
             : opcode == Opcode::MOVi64 ? x
             : opcode == Opcode::MOVZ ? movz
             : opcode == Opcode::MOVN ? movn
             : opcode == Opcode::MOVK ? movk
             : opcode == Opcode::MOVIv4Zero ? v
             : opcode == Opcode::MOVIv4s ? movi4s
             : opcode == Opcode::MOVIv4sMsl ? movimsl
             : opcode == Opcode::MVNIv4s ? mvni4s
             : opcode == Opcode::MOVIv16b ? movi16b
                                         : fmov4s;
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
    SIMPLE_CASE(MULXrr, "mul", 1, 3, 3, SchedResource::MAC)
    SIMPLE_CASE(MADDWrrr, "madd", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(MSUBWrrr, "msub", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(MSUBXrrr, "msub", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(SDIVWrr, "sdiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(UDIVWrr, "udiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(SDIVXrr, "sdiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(UDIVXrr, "udiv", 1, 3, 12, SchedResource::Divide)
    SIMPLE_CASE(SMULLXrr, "smull", 1, 3, 3, SchedResource::MAC)
    SIMPLE_CASE(SMADDLXrrr, "smaddl", 1, 4, 3, SchedResource::MAC)
    SIMPLE_CASE(UMULHXrr, "umulh", 1, 3, 3, SchedResource::MAC)
    SIMPLE_CASE(NEGX, "neg", 1, 2, 1, SchedResource::ALU)
    SIMPLE_CASE(ANDWrr, "and", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ANDWri, "and", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ORRWrr, "orr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ORRWri, "orr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(EORWrr, "eor", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ANDXrr, "and", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ORRXrr, "orr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ORRXri, "orr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(EORXrr, "eor", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSLWrr, "lsl", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSLWri, "lsl", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRWrr, "lsr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRWri, "lsr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ASRWrr, "asr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ASRWri, "asr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSLXrr, "lsl", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRXrr, "lsr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(ASRXrr, "asr", 1, 3, 1, SchedResource::ALU)
    SIMPLE_CASE(LSRXri, "lsr", 1, 3, 1, SchedResource::ALU)
    case Opcode::CMPWrr:
    case Opcode::CMPWri:
    case Opcode::CMPXrr:
    case Opcode::CMPXri:
    case Opcode::TSTWrr:
    case Opcode::TSTWri: {
        static const InstrDesc cmp{
            Opcode::CMPWrr, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc cmpi{
            Opcode::CMPWri, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc cmpx{
            Opcode::CMPXrr, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc cmpxi{
            Opcode::CMPXri, "cmp", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc tst{
            Opcode::TSTWrr, "tst", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        static const InstrDesc tsti{
            Opcode::TSTWri, "tst", 0, 2, false, false, false, false,
            false, false, false, false, true, false, 1,
            SchedResource::ALU};
        return opcode == Opcode::CMPWrr ? cmp
             : opcode == Opcode::CMPWri ? cmpi
             : opcode == Opcode::CMPXrr ? cmpx
             : opcode == Opcode::CMPXri ? cmpxi
             : opcode == Opcode::TSTWrr ? tst : tsti;
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
    InstrDesc dynamic;
    dynamic.opcode = opcode;
    dynamic.latency = 1;
    dynamic.resource = SchedResource::ALU;
    switch (opcode) {
    case Opcode::LDRWui: case Opcode::LDRWlo: case Opcode::LDRWpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRWro:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 5; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRSui: case Opcode::LDRSlo: case Opcode::LDRSpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRSro:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 5; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRDui:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRQui: case Opcode::LDRQlo: case Opcode::LDRQpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 5; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRQro:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 5; dynamic.mayLoad = true;
        dynamic.latency = 5; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRXui: case Opcode::LDRXlo: case Opcode::LDRXpost:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::LDRXro:
        dynamic.mnemonic = "ldr"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 5; dynamic.mayLoad = true;
        dynamic.latency = 4; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::STRWui: case Opcode::STRWlo: case Opcode::STRWpost:
    case Opcode::STRSui: case Opcode::STRSlo: case Opcode::STRSpost:
    case Opcode::STRDui:
    case Opcode::STRQui: case Opcode::STRQlo: case Opcode::STRQpost:
    case Opcode::STRXui: case Opcode::STRXlo: case Opcode::STRXpost:
        dynamic.mnemonic = "str"; dynamic.explicitOperands = 3;
        dynamic.mayStore = true; dynamic.resource = SchedResource::LoadStore;
        break;
    case Opcode::STRWro: case Opcode::STRSro:
    case Opcode::STRQro: case Opcode::STRXro:
        dynamic.mnemonic = "str"; dynamic.explicitOperands = 5;
        dynamic.mayStore = true; dynamic.resource = SchedResource::LoadStore;
        break;
    case Opcode::LDPWi: case Opcode::LDPSi:
    case Opcode::LDPXi: case Opcode::LDPDi: case Opcode::LDPQi:
        dynamic.mnemonic = "ldp"; dynamic.explicitDefs = 2;
        dynamic.explicitOperands = 4; dynamic.mayLoad = true;
        dynamic.latency = 5; dynamic.resource = SchedResource::LoadStore; break;
    case Opcode::STPWi: case Opcode::STPSi:
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
    case Opcode::DUPv4sLane:
        dynamic.mnemonic = "dup"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands =
            opcode == Opcode::DUPv4sLane ? 3 : 2;
        dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::INSv4i32: case Opcode::INSv4f32:
        dynamic.mnemonic = "ins"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::EXTRACTv4i32: case Opcode::EXTRACTv4f32:
        dynamic.mnemonic = "umov"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::ZIP1v4s: case Opcode::ZIP2v4s:
    case Opcode::UZP1v4s: case Opcode::UZP2v4s:
    case Opcode::TRN1v4s: case Opcode::TRN2v4s:
        dynamic.mnemonic =
            opcode == Opcode::ZIP1v4s ? "zip1"
            : opcode == Opcode::ZIP2v4s ? "zip2"
            : opcode == Opcode::UZP1v4s ? "uzp1"
            : opcode == Opcode::UZP2v4s ? "uzp2"
            : opcode == Opcode::TRN1v4s ? "trn1" : "trn2";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 6; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::EXTv16b:
        dynamic.mnemonic = "ext"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::REV64v4s:
        dynamic.mnemonic = "rev64"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::ADDv4i32: case Opcode::SUBv4i32:
    case Opcode::ADDv4f32: case Opcode::SUBv4f32:
    case Opcode::ANDv16i8: case Opcode::ORRv16i8: case Opcode::EORv16i8:
        dynamic.mnemonic = opcode == Opcode::ADDv4i32 || opcode == Opcode::ADDv4f32 ? "add"
                           : opcode == Opcode::SUBv4i32 || opcode == Opcode::SUBv4f32 ? "sub"
                           : opcode == Opcode::ANDv16i8 ? "and"
                           : opcode == Opcode::ORRv16i8 ? "orr" : "eor";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 6; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::MVNv16i8:
        dynamic.mnemonic = "mvn"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::CMEQv4i32: case Opcode::CMGTv4i32:
    case Opcode::CMGEv4i32: case Opcode::CMHIv4i32:
    case Opcode::CMHSv4i32: case Opcode::FCMEQv4f32:
    case Opcode::FCMGTv4f32: case Opcode::FCMGEv4f32:
        dynamic.mnemonic = "vector-compare"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::BSLv16i8:
        dynamic.mnemonic = "bsl"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::TBL1v16i8:
        dynamic.mnemonic = "tbl"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SMINv4i32: case Opcode::SMAXv4i32:
        dynamic.mnemonic =
            opcode == Opcode::SMINv4i32 ? "smin" : "smax";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 6; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::MULv4i32: case Opcode::MLAv4i32: case Opcode::MLSv4i32:
    case Opcode::MULv4f32:
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
        dynamic.latency = 6; dynamic.resource = SchedResource::FPMulDiv; break;
    case Opcode::DIVv4f32:
        dynamic.mnemonic = "fdiv"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 3; dynamic.latency = 18;
        dynamic.resource = SchedResource::FPMulDiv; break;
    case Opcode::NEGv4f32:
        dynamic.mnemonic = "fneg"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::NEGv4i32:
        dynamic.mnemonic = "neg"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SSHLv4i32: case Opcode::USHLv4i32:
        dynamic.mnemonic =
            opcode == Opcode::SSHLv4i32 ? "sshl" : "ushl";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 6; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SHLiv4i32:
    case Opcode::SSHRiv4i32:
    case Opcode::USHRiv4i32:
        dynamic.mnemonic = opcode == Opcode::SHLiv4i32 ? "shl"
                           : opcode == Opcode::SSHRiv4i32 ? "sshr" : "ushr";
        dynamic.explicitDefs = 1; dynamic.explicitOperands = 3;
        dynamic.latency = 6; dynamic.resource = SchedResource::FPALU; break;
    case Opcode::SHUFFLEv16i8:
        dynamic.mnemonic = "tbl"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 4; dynamic.latency = 6;
        dynamic.resource = SchedResource::FPALU; break;
    case Opcode::ADDVv4i32:
        dynamic.mnemonic = "addv"; dynamic.explicitDefs = 1;
        dynamic.explicitOperands = 2; dynamic.latency = 6;
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

InstrDesc InstrInfo::get(Opcode opcode) {
    return descriptor(opcode);
}

bool InstrInfo::acceptsImmediate(Opcode opcode, std::int64_t immediate) {
    switch (opcode) {
    case Opcode::ADDWri:
    case Opcode::SUBWri:
    case Opcode::ADDXri:
    case Opcode::SUBXri:
    case Opcode::CMPWri:
    case Opcode::CMPXri:
    case Opcode::SUBSPri:
    case Opcode::ADDSPri:
        return immediate >= 0 && immediate <= 4095;
    case Opcode::LSLXri:
    case Opcode::LSRXri:
    case Opcode::ASRXri:
        return immediate >= 0 && immediate <= 63;
    case Opcode::LSLWri:
    case Opcode::LSRWri:
    case Opcode::ASRWri:
        return immediate >= 0 && immediate <= 31;
    case Opcode::TSTWri:
        return immediate >= 0 &&
               immediate <= UINT32_MAX &&
               isLogicalImmediate32(
                   static_cast<std::uint32_t>(immediate));
    default:
        return false;
    }
}

bool InstrInfo::isCommutable(Opcode opcode) {
    switch (opcode) {
    case Opcode::ADDWrr:
    case Opcode::ADDXrr:
    case Opcode::MULWrr:
    case Opcode::MULXrr:
    case Opcode::ANDWrr:
    case Opcode::ANDXrr:
    case Opcode::ORRWrr:
    case Opcode::ORRXrr:
    case Opcode::EORWrr:
    case Opcode::EORXrr:
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

CondCode InstrInfo::inverseCondition(CondCode condition) {
    switch (condition) {
    case CondCode::EQ: return CondCode::NE;
    case CondCode::NE: return CondCode::EQ;
    case CondCode::HS: return CondCode::LO;
    case CondCode::LO: return CondCode::HS;
    case CondCode::MI: return CondCode::PL;
    case CondCode::PL: return CondCode::MI;
    case CondCode::VS: return CondCode::VC;
    case CondCode::VC: return CondCode::VS;
    case CondCode::HI: return CondCode::LS;
    case CondCode::LS: return CondCode::HI;
    case CondCode::GE: return CondCode::LT;
    case CondCode::LT: return CondCode::GE;
    case CondCode::GT: return CondCode::LE;
    case CondCode::LE: return CondCode::GT;
    case CondCode::AL: return CondCode::AL;
    }
    return CondCode::AL;
}

} // namespace backend::aarch64
