// Builds the ordered MachineFunction pass list.
#pragma once

#include "machine_pass_manager.hpp"

namespace backend::aarch64 {

struct BackendOptions;

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          const BackendOptions &options);

} // namespace backend::aarch64
