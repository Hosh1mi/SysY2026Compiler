// This file defines the slot numbering and live-range representation shared
// by register allocation, splitting, rematerialization, and spill rewriting.
#pragma once

#include "machine_ir.hpp"

#include <cstddef>
#include <set>
#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

using MachineSlot = unsigned;

struct MachineSlotRange {
  MachineSlot begin = 0;
  MachineSlot end = 0;

  bool contains(MachineSlot slot) const {
    return begin <= slot && slot < end;
  }

  bool overlaps(const MachineSlotRange &other) const {
    return begin < other.end && other.begin < end;
  }
};

// Instructions receive four ordered positions.  Uses precede ordinary defs,
// while early-clobber defs precede uses.  Keeping these positions distinct is
// required when a live range is later cut immediately before or after one
// instruction; a single instruction number cannot express those boundaries.
class MachineSlotIndexes {
public:
  void number(MachineFunction &function);

  MachineSlot earlyDefSlot(const MachineInstr &instruction) const;
  MachineSlot useSlot(const MachineInstr &instruction) const;
  MachineSlot defSlot(const MachineInstr &instruction) const;
  MachineSlot deadSlot(const MachineInstr &instruction) const;
  MachineSlotRange blockRange(const MachineBasicBlock *block) const;

private:
  static constexpr MachineSlot kInstructionSpacing = 4;
  std::unordered_map<const MachineBasicBlock *, MachineSlotRange>
      blockRanges_;
};

struct LiveRangeOperand {
  MachineBasicBlock *block = nullptr;
  MachineInstr *instruction = nullptr;
  unsigned operandIndex = 0;
  MachineSlot slot = 0;
  bool isDef = false;

  MachineOperand &operand() const;
};

struct LiveInterval {
  VReg reg = 0;
  RegClass regClass = RegClass::Invalid;
  double weight = 0.0;
  double spillCost = 0.0;
  bool crossesCall = false;
  std::vector<MachineSlotRange> segments;
  std::vector<LiveRangeOperand> operands;

  bool overlaps(const LiveInterval &other) const;
  bool liveAt(MachineSlot slot) const;
};

struct LivenessResult {
  std::vector<LiveInterval> intervals;
  std::unordered_map<VReg, std::size_t> intervalIndex;
  std::unordered_map<MachineBasicBlock *, std::set<VReg>> blockLiveIn;
  std::unordered_map<MachineBasicBlock *, std::set<VReg>> blockLiveOut;
  MachineSlotIndexes slots;

  const LiveInterval *find(VReg reg) const;
};

class MachineLiveness {
public:
  // Reuse existing loop depths when only instructions changed since the
  // previous run; CFG edits require a refresh.
  LivenessResult run(MachineFunction &function,
                     bool refreshLoopInfo = true) const;
};

} // namespace backend::aarch64
