#pragma once

#include "selection_dag.hpp"

#include <memory>

namespace backend::aarch64 {

class AArch64InstructionSelector {
public:
    std::unique_ptr<MachineFunction> select(FunctionDAG &functionDAG) const;
};

} // namespace backend::aarch64
