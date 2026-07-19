#pragma once

#include "machine.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

static const char *const kMachineFlagsReg = "$flags";

struct MachineLivenessResult {
    std::vector<std::set<std::string>> blockLiveIn;
    std::vector<std::set<std::string>> blockLiveOut;
    std::map<const MachineInstr *, std::set<std::string>> instrLiveOut;
    std::map<std::string, std::set<std::string>> labelLiveIn;
};

class MachineLiveness {
public:
    MachineLivenessResult analyze(const MachineFunction &func) const;
};

class MachineAnalysisManager {
public:
    const MachineLivenessResult &liveness(const MachineFunction &func);
    void invalidate();

private:
    const MachineFunction *function_ = nullptr;
    std::optional<MachineLivenessResult> liveness_;
};
