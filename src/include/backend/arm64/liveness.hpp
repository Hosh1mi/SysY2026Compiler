#pragma once

#include "machine.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

static const char *const kMachineFlagsReg = "$flags";

struct MachineLivenessResult {
    std::vector<std::set<std::string>> blockLiveIn;
    std::vector<std::set<std::string>> blockLiveOut;
    std::map<const MachineInstr *, std::set<std::string>> instrLiveOut;
};

class MachineLiveness {
public:
    MachineLivenessResult analyze(const MachineFunction &func) const;

private:
    std::vector<std::vector<size_t>> computeSuccessors(const MachineFunction &func) const;
};
