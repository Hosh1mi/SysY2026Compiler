// This file defines reusable control-flow analyses for MachineFunction passes.
// The analyses mirror the backend-facing dominance and natural-loop concepts
// used by LLVM while remaining independent of any particular optimization.
#pragma once

#include "machine_ir.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {

class MachineDominatorTree {
public:
  void analyze(const MachineFunction &function);
  bool isReachable(const MachineBasicBlock *block) const;
  bool dominates(const MachineBasicBlock *dominator,
                 const MachineBasicBlock *block) const;

private:
  using BlockSet = std::unordered_set<const MachineBasicBlock *>;
  BlockSet reachable_;
  std::unordered_map<const MachineBasicBlock *, BlockSet> dominators_;
};

struct MachineLoop {
  MachineBasicBlock *header = nullptr;
  std::unordered_set<MachineBasicBlock *> blocks;
};

class MachineLoopInfo {
public:
  void analyze(MachineFunction &function,
               const MachineDominatorTree &dominators);
  const std::vector<MachineLoop> &loops() const { return loops_; }
  unsigned depth(const MachineBasicBlock *block) const;

private:
  std::vector<MachineLoop> loops_;
  std::unordered_map<const MachineBasicBlock *, unsigned> depths_;
};

} // namespace backend::aarch64
