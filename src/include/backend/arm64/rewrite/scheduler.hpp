#pragma once

#include "machine_ir.hpp"

#include <cstdint>

namespace backend::aarch64 {

enum class SchedulingStage : std::uint8_t {
    PreRA,
    PostRA,
};

struct A53SchedInfo {
    unsigned microOps = 1;
    unsigned latency = 1;
    unsigned resourceCycles = 1;
    SchedResource resource = SchedResource::ALU;
};

/// Cortex-A53's in-order, two-wide execution model.  The model is deliberately
/// independent of the scheduling policy so instruction costs can also be used
/// by diagnostics and future machine transforms.
class A53SchedulingModel {
public:
    static constexpr unsigned issueWidth() { return 2; }

    A53SchedInfo describe(const MachineInstr &instruction) const;
    unsigned resourceCapacity(SchedResource resource) const;
    unsigned dependencyLatency(const MachineInstr &producer,
                               const MachineInstr &consumer,
                               std::uint64_t registerKey) const;
};

class A53MachineScheduler {
public:
    bool run(MachineFunction &function, SchedulingStage stage) const;
};

} // namespace backend::aarch64
