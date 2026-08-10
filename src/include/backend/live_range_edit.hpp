// This file defines the mutation boundary for splitting one virtual live
// range into siblings while keeping register metadata and spill slots valid.
#pragma once

#include "live_range.hpp"

#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

struct LocalSplitPlan {
  VReg parent = 0;
  MachineBasicBlock *block = nullptr;
  MachineInstr *splitAfter = nullptr;
  MachineInstr *resumeBefore = nullptr;
  MachineSlot resumeSlot = 0;
  double estimatedBenefit = 0.0;
  double estimatedCost = 0.0;
  std::vector<VReg> relievedSpills;
};

struct LiveRangeSplitPlans {
  std::vector<LocalSplitPlan> local;

  bool empty() const { return local.empty(); }
};

class LiveRangeEdit {
public:
  bool splitLocalGap(MachineFunction &function,
                     const LivenessResult &liveness,
                     const LocalSplitPlan &plan,
                     std::unordered_map<VReg, int> &spillSlots) const;
};

} // namespace backend::aarch64
