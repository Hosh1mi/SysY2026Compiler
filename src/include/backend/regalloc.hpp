#pragma once

#include "live_range.hpp"
#include "live_range_edit.hpp"
#include "live_range_split_analysis.hpp"

#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

class PhiElimination {
public:
    static bool run(MachineFunction &function);
};

/// Chaitin-Briggs optimistic global graph-coloring allocator over typed
/// Machine virtual registers.  COPY affinities bias color selection; spill
/// candidates are chosen only during simplify and are retried optimistically
/// during select before spill code is inserted.
class GraphColoringRegisterAllocator {
public:
    static bool run(MachineFunction &function);

private:
    static bool colorOnce(
        MachineFunction &function, const LivenessResult &liveness,
        std::unordered_map<VReg, PhysReg> &assignments,
        std::vector<VReg> &spills,
        LiveRangeSplitPlans &splitPlans);
    static void insertSpills(MachineFunction &function,
                             const std::vector<VReg> &spills,
                             std::unordered_map<VReg, int> &spillSlots);
    static void rewriteVirtualRegisters(
        MachineFunction &function,
        const std::unordered_map<VReg, PhysReg> &assignments);
};

class PostRAParallelCopyResolver {
public:
    static bool run(MachineFunction &function);
};

} // namespace backend::aarch64
