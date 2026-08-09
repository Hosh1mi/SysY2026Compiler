#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace backend::aarch64 {

// 值类型：DAG 节点结果类型、VReg 类型；由 isel/RegisterInfo 映射到 RegClass
enum class ValueType : std::uint8_t {
    Invalid, // 占位（如 chain 结果）
    I1,      // 比较/布尔
    I32,
    I64,
    F32,
    Ptr,   // 指针（按 64 位地址处理）
    V4I32, // 4×i32 NEON
    V4F32, // 4×f32 NEON
    Flags, // NZCV 条件标志
};

// 寄存器类：VReg/物理操作数所属银行；RA 按类着色，打印按类选 w/x/s/v 视图
enum class RegClass : std::uint8_t {
    Invalid,
    GPR32,   // W 寄存器
    GPR64,   // X / SP
    FPR32,   // S 标量浮点
    NEON128, // V 128-bit 向量
    CCR,     // NZCV
};

// 物理寄存器编号：RA 着色结果、ABI 传参/返回、帧保存；asm_printer 据此输出名字
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

// 机器指令操作码：MachineInstr 的核心字段；isel 发射、各 Machine Pass 匹配、
// InstrInfo/调度/打印/校验均查此枚举
enum class Opcode : std::uint16_t {
    Invalid,
    // --- 伪指令 / SSA / 调用帧（帧降低或 PostRA 前消除）---
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
    MOVi32, // isel 常量；PostRA 再选择 move-wide 或 logical-immediate
    MOVi64,
    MOVZ,
    MOVN,
    MOVK,
    MOVIv4Zero,
    MOVIv4s,
    MOVIv4sMsl,
    MVNIv4s,
    MOVIv16b,
    FMOVv4s,
    ADRP,
    ADDlow,
    LEA_FRAME, // 栈槽地址；frame_lowering 换成 ADD/MOV 相对 SP/FP
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
    MULXrr,
    MADDWrrr,
    MSUBWrrr,
    MSUBXrrr,
    SDIVWrr,
    UDIVWrr,
    SDIVXrr,
    UDIVXrr,
    SMULLXrr,
    SMADDLXrrr,
    UMULHXrr,
    NEGX,
    CMPXrr,
    CMPXri,
    ANDWrr,
    ANDWri,
    ORRWrr,
    ORRWri,
    EORWrr,
    ANDXrr,
    ORRXrr,
    ORRXri,
    EORXrr,
    LSLWrr,
    LSLWri,
    LSRWrr,
    LSRWri,
    ASRWrr,
    ASRWri,
    LSLXrr,
    LSRXrr,
    ASRXrr,
    LSRXri,
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
    // 访存后缀：ui=基址+立即数，lo=全局 lo12，ro=寄存器偏移，post=后变址
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
    LDRQro,
    LDRQpost,
    STRQui,
    STRQlo,
    STRQro,
    STRQpost,
    LDRXui,
    LDRXlo,
    LDRXro,
    LDRXpost,
    STRXui,
    STRXlo,
    STRXro,
    STRXpost,
    LDPWi,
    STPWi,
    LDPSi,
    STPSi,
    LDPXi,
    STPXi,
    LDPDi,
    STPDi,
    STPXpre,  // 序言：STP 前变址压栈
    LDPXpost, // 收尾：LDP 后变址出栈
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
    DUPv4sLane,
    INSv4i32,
    INSv4f32,
    EXTRACTv4i32,
    EXTRACTv4f32,
    ZIP1v4s,
    ZIP2v4s,
    UZP1v4s,
    UZP2v4s,
    TRN1v4s,
    TRN2v4s,
    EXTv16b,
    REV64v4s,
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
    MVNv16i8,
    CMEQv4i32,
    CMGTv4i32,
    CMGEv4i32,
    CMHIv4i32,
    CMHSv4i32,
    FCMEQv4f32,
    FCMGTv4f32,
    FCMGEv4f32,
    BSLv16i8,
    TBL1v16i8,
    SHUFFLEv16i8,
    ADDVv4i32,
    FRAME_SETUP,   // 保留伪指令；当前帧降低直接插 STP/SUB
    FRAME_DESTROY,
    SPILL_LOAD,  // RA 溢出；frame_lowering 换成真 LDR
    SPILL_STORE,
};

// AArch64 条件码：Bcc/CSEL/CSET 等操作数；isel 与条件相关 Pass 使用。
enum class CondCode : std::uint8_t {
    EQ, NE, HS, LO, MI, PL, VS, VC,
    HI, LS, GE, LT, GT, LE, AL,
};

// 调度资源类别：写入 InstrDesc，供 A53MachineScheduler 做软偏好。
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

// 单条 Opcode 的静态属性表项；由 InstrInfo::get 返回，供 MIR 分类、调度、校验、打印。
struct InstrDesc {
    Opcode opcode = Opcode::Invalid;
    std::string_view mnemonic; // 汇编助记符或伪名
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

// 寄存器查询工具：类型/ABI/名字/分配序；isel、RA、帧降低、asm_printer 共用。
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

// 指令查询工具：描述表、立即数合法性、是否可交换；isel/peephole/调度等使用。
class InstrInfo {
public:
    static const InstrDesc &get(Opcode opcode);
    static bool acceptsImmediate(Opcode opcode, std::int64_t immediate);
    static bool isCommutable(Opcode opcode);
};

} // namespace backend::aarch64
