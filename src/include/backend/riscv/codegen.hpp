#pragma once
// RISC-V 后端驱动：遍历 Module，发射数据段与各函数代码到输出流。
//
// 对外接口与 ARM 后端（Arm64CodeGen）保持一致的方法名，使 main.cpp 能以同一套
// 配置代码切换后端。框架阶段只走朴素（无寄存器分配）代码生成路径，setNo*/set
// EnableRegAlloc 等开关被记录但暂不驱动后端优化（后续接入）。

#include "../../mid/ir/ir.hpp"
#include "machine.hpp"

#include <ostream>

class RiscvCodeGen {
public:
    RiscvCodeGen(Module *m, std::ostream &os) : m_(m), os_(os) {}
    void generate();

    void setNoPeephole(bool v) { no_peephole_ = v; }
    void setEnableRegAlloc(bool v) { enable_regalloc_ = v; }
    void setNoSchedule(bool v) { no_schedule_ = v; }
    void setNoPreSchedule(bool v) { no_pre_schedule_ = v; }
    void setDumpMachineInstr(bool v) { dump_machine_instr_ = v; }
    void setDumpPreMachineInstr(bool v) { dump_pre_machine_instr_ = v; }

private:
    void emitGlobal(GlobalVariable *gv);

    Module *m_;
    std::ostream &os_;
    bool no_peephole_ = false;
    bool enable_regalloc_ = true;
    bool no_schedule_ = true;
    bool no_pre_schedule_ = true;
    bool dump_machine_instr_ = false;
    bool dump_pre_machine_instr_ = false;
};
