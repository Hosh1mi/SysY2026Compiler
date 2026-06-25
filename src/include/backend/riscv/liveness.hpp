#pragma once

#include "machine.hpp"

#include <map>
#include <set>

namespace riscv {

struct LivenessResult {
    std::map<const MInst *, std::set<std::string>> liveOut;
};

LivenessResult analyzeLiveness(const MFunction &func);

}  // namespace riscv
