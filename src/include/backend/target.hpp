#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "aarch64_opcodes.hpp"

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

// Operand constraints are part of the opcode contract, not properties of a
// particular selection pattern.  Negative indices mean no such constraint.
struct OperandConstraint {
    int tiedUse = -1;
    int tiedDef = -1;
    int earlyClobberDef = -1;
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
    OperandConstraint operands;
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
    static bool isArgumentRegister(PhysReg reg);
    static bool isReturnRegister(PhysReg reg);
    static PhysReg integerArgumentRegister(unsigned index);
    static PhysReg vectorArgumentRegister(unsigned index);
    static std::string_view name(PhysReg reg, RegClass view);
    static unsigned spillSize(RegClass regClass);
    static unsigned spillAlignment(RegClass regClass);
    static const std::vector<PhysReg> &allocationOrder(
        RegClass regClass, bool preferCallerSaved = false);
    static const std::vector<PhysReg> &calleeSaved(RegClass regClass);
};

// 指令查询工具：描述表、立即数合法性、是否可交换；isel/peephole/调度等使用。
class InstrInfo {
public:
    static const InstrDesc &get(Opcode opcode);
    static bool acceptsImmediate(Opcode opcode, std::int64_t immediate);
    static bool isCommutable(Opcode opcode);
    // Zero means that cloning this opcode at a use is not legal.  A positive
    // result estimates its target cost relative to a spill reload.
    static unsigned rematerializationCost(Opcode opcode);
    static CondCode inverseCondition(CondCode condition);
};

} // namespace backend::aarch64
