#pragma once
// RISC-V 后端驱动：遍历 Module，发射数据段与各函数代码到输出流。
//
// 对外接口与 ARM 后端（Arm64CodeGen）保持一致的方法名。RISC-V 的寄存器分配、
// 机器 DCE、低风险窥孔和 machine dump 均由这些开关显式控制；调度开关保留给
// 后续基于 BOOM 资源模型的实现。

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
