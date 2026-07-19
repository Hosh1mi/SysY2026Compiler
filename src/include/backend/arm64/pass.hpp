#pragma once

#include "liveness.hpp"
#include "machine.hpp"

#include <string>

class Arm64MachineFunctionPass {
public:
    virtual ~Arm64MachineFunctionPass() = default;
    virtual const char *name() const = 0;
    virtual bool run(MachineFunction &function,
                     MachineAnalysisManager &analyses) = 0;
};
