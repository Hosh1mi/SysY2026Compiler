#pragma once
// RISC-V 机器层表示。
//
// 代码以显式的 MachineBasicBlock CFG 组织：每个块带一个标签头、一段直线指令、
// 以及由 buildCFG 计算的前驱/后继索引。标签不再是指令，而是块的属性；汇编文本
// 只在最终打印时生成。优化与分析使用结构化 opcode、defs/uses 与副作用标志，
// 块边界由 CFG 给出，而不再依赖对汇编文本反向解析标签段。

#include <set>
#include <string>
#include <vector>

namespace riscv {

enum class MOpcode {
    Unknown,
    Directive,
    Move,
    Alu,
    Mul,
    Div,
    Load,
    Store,
    Address,
    Branch,
    Call,
    Ret,
};

// 一条机器指令。text 为不含前导制表符与换行的单行汇编（最终打印用）；mnemonic 与
// operands 为结构化拆分；defs/uses 为受影响的物理寄存器（ABI 名，fp 规范化为 s0）。
struct MInst {
    std::string text;
    std::string mnemonic;
    std::vector<std::string> operands;
    MOpcode opcode = MOpcode::Unknown;
    std::set<std::string> defs;
    std::set<std::string> uses;
    bool isDirective = false;  // ".globl ..." 等伪指令，打印时不缩进
    bool mayLoad = false;
    bool mayStore = false;
    bool isCall = false;
    bool isTerminator = false;  // 无条件结束本块的指令：j / ret
    bool isBarrier = false;

    // 条件分支（beq/bne/.../bnez）：是 Branch 但非 terminator，块在其后贯穿。
    bool isConditionalBranch() const { return opcode == MOpcode::Branch && !isTerminator; }
    // 该指令是否带一个标签目标操作数（分支/跳转）。
    bool hasLabelTarget() const { return opcode == MOpcode::Branch && !operands.empty(); }

    static MInst inst(std::string t);
    static MInst directive(std::string d);
};

// 一个基本块：标签头 + 直线指令序列 + CFG 邻接（索引进 MFunction::blocks）。
// 本后端采用“标签段”块粒度：一个块可含中途的条件分支，因而可能有多个后继。
struct MBasicBlock {
    std::string label;
    std::vector<MInst> insts;
    std::vector<int> succ;
    std::vector<int> pred;

    bool hasLabel() const { return !label.empty(); }
};

// 一个函数的机器代码：函数级前导伪指令（.globl）+ 显式基本块序列。
struct MFunction {
    std::string name;
    std::vector<MInst> directives;
    std::vector<MBasicBlock> blocks;

    // ── lowering 期发射 API ────────────────────────────────────────────────
    void addDirective(std::string text);
    void startBlock(std::string label);  // 以给定标签开启新块
    void push(MInst m);                   // 追加到当前（最后一个）块
    MBasicBlock &cur();                    // 当前块（必要时创建无标签入口块）
};

// 由各块的分支/终结指令重算 succ/pred。
void buildCFG(MFunction &func);

// 结构验证：拒绝未解析的分支目标、贯穿出函数末尾、缺失副作用描述（Unknown 非
// barrier）。返回 true 表示通过，否则 error 填入首个错误。
bool verifyMFunction(const MFunction &func, std::string &error);

// 渲染整个函数为汇编文本。
std::string printMFunction(const MFunction &func);
std::string dumpMFunction(const MFunction &func);

}  // namespace riscv
