#pragma once
#include "machine.hpp"
#include "../../mid/ir/ir.hpp"
#include <ostream>

class Arm64CodeGen {
public:
    Arm64CodeGen(Module *m, std::ostream &os) : m_(m), os_(os) {}
    void generate();

    void setNoPeephole(bool v) { no_peephole_ = v; }
    void setEnableRegAlloc(bool v) { enable_regalloc_ = v; }
    void setNoSchedule(bool v) { no_schedule_ = v; }
    void setDumpMachineInstr(bool v) { dump_machine_instr_ = v; }

private:
    void emitGlobal(MachineModule &module, GlobalVariable *gv);
    void emitExtern(Function *f);

    Module *m_;
    std::ostream &os_;
    bool no_peephole_ = false;
    bool enable_regalloc_ = true;
    bool no_schedule_ = true;
    bool dump_machine_instr_ = false;
};
