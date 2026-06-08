#pragma once

#include "machine.hpp"

#include <vector>

class MachineScheduler {
public:
    void schedule(MachineFunction &func) const;

private:
    std::vector<MachineInstr> scheduleSegment(const std::vector<MachineInstr> &segment,
                                              bool preserveFlagLiveOut = false) const;
};
