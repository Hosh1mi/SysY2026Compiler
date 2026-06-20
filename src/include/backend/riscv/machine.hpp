#pragma once
// RISC-V 机器层表示（轻量）。
//
// 框架阶段的机器层刻意保持精简：每条指令携带一行渲染好的汇编文本，外加少量
// 分类标志（标签/伪指令/是否访存/是否调用/是否终结）。这足以支撑代码生成与
// 输出；后续接入窥孔、调度、寄存器分配等优化时，再在此基础上扩充 defs/uses 等
// 结构化元信息，而无需推翻整体框架。

#include <string>
#include <vector>

namespace riscv {

// 一条机器指令（或标签/伪指令）。text 为不含前导制表符与换行的单行汇编。
struct MInst {
    std::string text;
    bool isLabel = false;      // text 形如 "foo:"，打印时不缩进
    bool isDirective = false;  // text 为 ".globl ..." 等伪指令，打印时不缩进
    bool mayLoad = false;
    bool mayStore = false;
    bool isCall = false;
    bool isTerminator = false;

    static MInst inst(std::string t) { return MInst{std::move(t)}; }
    static MInst label(std::string l) {
        MInst m;
        m.text = std::move(l) + ":";
        m.isLabel = true;
        return m;
    }
    static MInst directive(std::string d) {
        MInst m;
        m.text = std::move(d);
        m.isDirective = true;
        return m;
    }
};

// 一个函数的机器代码：扁平的指令序列（含标签）。
struct MFunction {
    std::string name;
    std::vector<MInst> insts;

    void push(MInst m) { insts.push_back(std::move(m)); }
};

// 渲染整个函数为汇编文本。
std::string printMFunction(const MFunction &func);

}  // namespace riscv
