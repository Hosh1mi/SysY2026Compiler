#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace backend::aarch64 {

enum class ValueType : std::uint8_t {
    Invalid,
    I1,
    I32,
    F32,
    Ptr,
    V4I32,
    V4F32,
    Flags,
};

enum class RegClass : std::uint8_t {
    Invalid,
    GPR32,
    GPR64,
    FPR32,
    NEON128,
    CCR,
};

enum class PhysReg : std::uint16_t {
    NoReg,
    X0, X1, X2, X3, X4, X5, X6, X7,
    X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23,
    X24, X25, X26, X27, X28, X29, X30,
    SP,
    XZR,
    V0, V1, V2, V3, V4, V5, V6, V7,
    V8, V9, V10, V11, V12, V13, V14, V15,
    V16, V17, V18, V19, V20, V21, V22, V23,
    V24, V25, V26, V27, V28, V29, V30, V31,
    NZCV,
};

enum class Opcode : std::uint16_t {
    Invalid,
    PHI,
    COPY,
    IMPLICIT_DEF,
    ADJCALLSTACKDOWN,
    ADJCALLSTACKUP,
    CALL,
    TAILCALL,
    RET,
    B,
    Bcc,
    CBZ,
    CBNZ,
    TBZ,
    TBNZ,
    CSELW,
    CSELX,
    FCSELS,
    CSETW,
    MOVi32,
    MOVi64,
    MOVIv4Zero,
    ADRP,
    ADDlow,
    LEA_FRAME,
    ADDWrr,
    ADDWri,
    ADDWrs,
    ADDWrsX,
    ADDWlsl,
    SUBWrr,
    SUBWri,
    NEGW,
    CNEGW,
    MULWrr,
    MADDWrrr,
    MSUBWrrr,
    SDIVWrr,
    UDIVWrr,
    SMULLXrr,
    SMADDLXrrr,
    ANDWrr,
    ANDWri,
    ORRWrr,
    EORWrr,
    LSLWrr,
    LSLWri,
    LSRWrr,
    LSRWri,
    ASRWrr,
    ASRWri,
    CMPWrr,
    CMPWri,
    TSTWrr,
    TSTWri,
    CLZW,
    RBITW,
    FADDS,
    FSUBS,
    FMULS,
    FDIVS,
    FNEGS,
    FCMPSrr,
    FCMPZS,
    SCVTFWS,
    FCVTZSW,
    FMOVWS,
    FMOVSW,
    LDRWui,
    LDRWlo,
    LDRWro,
    LDRWpost,
    STRWui,
    STRWlo,
    STRWro,
    STRWpost,
    LDRSui,
    LDRSlo,
    LDRSro,
    LDRSpost,
    STRSui,
    STRSlo,
    STRSro,
    STRSpost,
    LDRDui,
    STRDui,
    LDRQui,
    LDRQlo,
    LDRQpost,
    STRQui,
    STRQlo,
    STRQpost,
    LDRXui,
    LDRXlo,
    LDRXpost,
    STRXui,
    STRXlo,
    STRXpost,
    LDPXi,
    STPXi,
    LDPDi,
    STPDi,
    STPXpre,
    LDPXpost,
    LDPQi,
    STPQi,
    ADDXrr,
    ADDXri,
    ADDXrs,
    SUBXrr,
    SUBXri,
    LSLXri,
    ASRXri,
    COPYXtoW,
    MOVXrr,
    SUBSPri,
    ADDSPri,
    SXTW,
    UXTW,
    DUPv4i32,
    DUPv4f32,
    INSv4i32,
    INSv4f32,
    EXTRACTv4i32,
    EXTRACTv4f32,
    ADDv4i32,
    SUBv4i32,
    MULv4i32,
    SMINv4i32,
    SMAXv4i32,
    NEGv4i32,
    SSHLv4i32,
    USHLv4i32,
    SHLiv4i32,
    SSHRiv4i32,
    USHRiv4i32,
    MLAv4i32,
    MLSv4i32,
    ADDv4f32,
    SUBv4f32,
    MULv4f32,
    DIVv4f32,
    NEGv4f32,
    FMLAv4f32,
    FMLSv4f32,
    ANDv16i8,
    ORRv16i8,
    EORv16i8,
    SHUFFLEv16i8,
    ADDVv4i32,
    FRAME_SETUP,
    FRAME_DESTROY,
    SPILL_LOAD,
    SPILL_STORE,
};

enum class CondCode : std::uint8_t {
    EQ, NE, HS, LO, MI, PL, VS, VC,
    HI, LS, GE, LT, GT, LE, AL,
};

enum class SchedResource : std::uint8_t {
    None,
    ALU,
    MAC,
    Divide,
    LoadStore,
    Branch,
    FPALU,
    FPMulDiv,
};

struct InstrDesc {
    Opcode opcode = Opcode::Invalid;
    std::string_view mnemonic;
    unsigned explicitDefs = 0;
    unsigned explicitOperands = 0;
    bool terminator = false;
    bool branch = false;
    bool call = false;
    bool returnInstruction = false;
    bool mayLoad = false;
    bool mayStore = false;
    bool hasSideEffects = false;
    bool pseudo = false;
    bool setsFlags = false;
    bool usesFlags = false;
    unsigned latency = 1;
    SchedResource resource = SchedResource::ALU;
};

class RegisterInfo {
public:
    static RegClass classForType(ValueType type);
    static bool isGPR(PhysReg reg);
    static bool isVector(PhysReg reg);
    static bool aliases(PhysReg lhs, PhysReg rhs);
    static bool isReserved(PhysReg reg);
    static bool isCallerSaved(PhysReg reg);
    static bool isCalleeSaved(PhysReg reg);
    static std::string_view name(PhysReg reg, RegClass view);
    static const std::vector<PhysReg> &allocationOrder(RegClass regClass);
    static const std::vector<PhysReg> &calleeSaved(RegClass regClass);
};

class InstrInfo {
public:
    static const InstrDesc &get(Opcode opcode);
    static bool acceptsImmediate(Opcode opcode, std::int64_t immediate);
    static bool isCommutable(Opcode opcode);
};

} // namespace backend::aarch64
