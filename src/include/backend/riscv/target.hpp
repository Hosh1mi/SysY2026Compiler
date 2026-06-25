#pragma once
// RV64GC 目标描述：寄存器 ABI 名、调用约定相关的寄存器分组。
//
// 目标平台为 BOOM v3（RISC-V RV64GC，乱序双发射，带 IEEE754 FPU，无 SIMD）。
// SysY 的整型为 32 位（i32），浮点为 32 位（float），指针为 64 位。
//   - i32 运算使用 *w 形式指令（addw/subw/...），结果符号扩展到 64 位寄存器；
//   - float 运算使用 .s 形式（fadd.s/...）；
//   - 地址/指针使用 64 位整型指令（add/ld/sd）。
//
// 这里只描述与代码生成框架相关的寄存器约定，具体指令选择见 context.cpp。

#include <array>
#include <optional>
#include <set>
#include <string>

namespace riscv {

// 寄存器类别：通用整型寄存器（含地址）与浮点寄存器。
enum class RegClass {
    GPR,  // x0-x31
    FPR,  // f0-f31
};

// ── 物理寄存器描述（ABI 名 ↔ xN/fN 别名、寄存器类、调用钳制）──────────────────
// 单一事实来源，供 lowering 副作用标注、活跃性、寄存器分配与机器层验证共用。

// 名字是否为一个物理寄存器（ABI 名、x0-x31、f0-f31，或别名 fp）。
bool isReg(const std::string &name);

// 寄存器类别；非寄存器返回 nullopt。
std::optional<RegClass> regClassOf(const std::string &name);

// 规范化为 ABI 名：xN/fN 数字名映射到 ABI 名，fp→s0；已是 ABI 名或非寄存器原样返回。
std::string canonicalReg(const std::string &name);

// 调用钳制：一次 call 覆盖的 caller-saved 物理寄存器集合（GPR: ra/t0-t6/a0-a7，
// FPR: ft0-ft11/fa0-fa7），均为规范 ABI 名。
const std::set<std::string> &callClobbers();

// caller-saved / callee-saved 判定（输入会先规范化）。sp/gp/tp/zero 等专用寄存器
// 两者均为 false。
bool isCallerSaved(const std::string &name);
bool isCalleeSaved(const std::string &name);

// ── 调用约定（LP64D）相关寄存器 ──────────────────────────────────────────
// 整型/指针实参依次放入 a0-a7，溢出走栈；浮点实参依次放入 fa0-fa7，溢出走栈。
// 返回值：整型 a0、浮点 fa0。
inline constexpr int kNumIntArgRegs = 8;
inline constexpr int kNumFpArgRegs = 8;

inline const std::array<const char *, kNumIntArgRegs> &intArgRegs() {
    static const std::array<const char *, kNumIntArgRegs> regs = {
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
    return regs;
}

inline const std::array<const char *, kNumFpArgRegs> &fpArgRegs() {
    static const std::array<const char *, kNumFpArgRegs> regs = {
        "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"};
    return regs;
}

// 返回值寄存器
inline const char *intRetReg() { return "a0"; }
inline const char *fpRetReg() { return "fa0"; }

// ── 固定用途寄存器 ───────────────────────────────────────────────────────
inline const char *zeroReg() { return "zero"; }  // x0，恒为 0
inline const char *raReg() { return "ra"; }       // x1，返回地址
inline const char *spReg() { return "sp"; }       // x2，栈指针
inline const char *fpReg() { return "s0"; }       // x8，帧指针

// ── 朴素（-O0）代码生成使用的临时寄存器划分 ─────────────────────────────
// 朴素选择器在单条 IR 指令内部把操作数加载到固定临时寄存器、计算、再写回栈槽，
// 不跨指令保留寄存器状态。各组互不重叠，便于地址/大立即数物化与数据搬运并存。
//   - 整型操作数/结果： t0, t1, t2
//   - 地址运算：        t3, t4
//   - 大栈偏移/大立即数物化（仅供 load/store 帧寻址内部使用）：t5, t6
//   - 浮点操作数/结果： ft0, ft1, ft2
namespace scratch {
inline const char *int0() { return "t0"; }
inline const char *int1() { return "t1"; }
inline const char *int2() { return "t2"; }
inline const char *addr0() { return "t3"; }
inline const char *addr1() { return "t4"; }
inline const char *frame0() { return "t5"; }  // 帧寻址物化的中间地址
inline const char *frame1() { return "t6"; }  // 大立即数物化
inline const char *fp0() { return "ft0"; }
inline const char *fp1() { return "ft1"; }
inline const char *fp2() { return "ft2"; }
}  // namespace scratch

// RV 立即数寻址的 12 位有符号范围：[-2048, 2047]。
inline bool fitsImm12(long v) { return v >= -2048 && v <= 2047; }

}  // namespace riscv
