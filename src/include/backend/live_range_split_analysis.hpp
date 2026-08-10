// This file defines profitability analysis for live-range cuts proposed after
// graph coloring has exposed the actual physical-register blockers.
#pragma once

#include "interference_graph.hpp"
#include "live_range_edit.hpp"

#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {

using ForbiddenColorMap =
    std::unordered_map<VReg, std::unordered_set<PhysReg>>;

class LiveRangeSplitAnalysis {
public:
  LiveRangeSplitPlans analyze(
      MachineFunction &function, const LivenessResult &liveness,
      const InterferenceGraph &graph,
      const std::unordered_map<VReg, PhysReg> &assignments,
      const std::vector<VReg> &spills,
      const ForbiddenColorMap &forbiddenColors) const;
};

} // namespace backend::aarch64
