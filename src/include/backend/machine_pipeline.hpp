// Owns machine-level transform instances and registers them onto a staged
// MachineFunctionPassManager, mirroring how the mid-end builds its pipeline.
#pragma once

#include "machine_pass_manager.hpp"

namespace backend::aarch64 {

struct BackendOptions;

void buildMachinePipeline(MachineFunctionPassManager &pipeline,
                          const BackendOptions &options);

} // namespace backend::aarch64
