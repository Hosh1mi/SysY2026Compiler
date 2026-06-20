#pragma once
// RISC-V 机器层表示。汇编文本只负责最终渲染；优化和分析使用结构化 opcode、
// defs/uses 与副作用标志。

#include <set>
#include <string>
#include <vector>

namespace riscv {

enum class MOpcode {
    Unknown,
    Label,
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

// 一条机器指令（或标签/伪指令）。text 为不含前导制表符与换行的单行汇编。
struct MInst {
    std::string text;
    std::string mnemonic;
    std::vector<std::string> operands;
    MOpcode opcode = MOpcode::Unknown;
    std::set<std::string> defs;
    std::set<std::string> uses;
    bool isLabel = false;      // text 形如 "foo:"，打印时不缩进
    bool isDirective = false;  // text 为 ".globl ..." 等伪指令，打印时不缩进
    bool mayLoad = false;
    bool mayStore = false;
    bool isCall = false;
    bool isTerminator = false;
    bool isBarrier = false;

    static MInst inst(std::string t);
    static MInst label(std::string l);
    static MInst directive(std::string d);
};

// 一个函数的机器代码：扁平的指令序列（含标签）。
struct MFunction {
    std::string name;
    std::vector<MInst> insts;

    void push(MInst m) { insts.push_back(std::move(m)); }
};

// 渲染整个函数为汇编文本。
std::string printMFunction(const MFunction &func);
std::string dumpMFunction(const MFunction &func);

}  // namespace riscv
