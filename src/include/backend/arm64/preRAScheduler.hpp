#pragma once

#include "machine.hpp"
#include "../../mid/ir/ir.hpp"

#include <iosfwd>
#include <map>
#include <string>

class PreRAScheduler {
public:
    struct VRegInfo {
        std::string name;
        MachineRegClass regClass = MachineRegClass::None;
    };

    explicit PreRAScheduler(bool dump = false, std::ostream *dumpOut = nullptr);

    bool run(Module *module);

private:
    bool scheduleFunction(Function *func);
    bool scheduleBlock(BasicBlock *bb, std::map<Value *, VRegInfo> &vregs,
                       int &nextVReg);
    void dumpMachineFunction(const MachineFunction &func) const;

    bool dump_ = false;
    std::ostream *dumpOut_ = nullptr;
};
