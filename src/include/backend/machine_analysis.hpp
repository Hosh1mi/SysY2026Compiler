// This file defines reusable control-flow analyses for MachineFunction passes.
// The analyses mirror the backend-facing dominance and natural-loop concepts
// used by LLVM while remaining independent of any particular optimization.
#pragma once

#include "machine_ir.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {

struct MachineRegisterReference {
  MachineBasicBlock *block = nullptr;
  MachineInstr *instruction = nullptr;
  unsigned operandIndex = 0;

  MachineOperand &operand() const;
};

// A rebuildable snapshot of virtual-register definitions and uses.  It keeps
// mutation cheap while giving passes one shared, non-dangling query surface.
class MachineRegisterIndex {
public:
  MachineRegisterIndex() = default;
  explicit MachineRegisterIndex(MachineFunction &function) {
    rebuild(function);
  }

  void rebuild(MachineFunction &function);
  const std::vector<MachineRegisterReference> &definitions(VReg reg) const;
  const std::vector<MachineRegisterReference> &uses(VReg reg) const;
  MachineInstr *uniqueDefinition(VReg reg) const;
  MachineBasicBlock *uniqueDefinitionBlock(VReg reg) const;
  unsigned useCount(VReg reg) const;
  bool allUsesHaveOpcode(VReg reg, Opcode opcode) const;
  unsigned replaceUses(VReg from, VReg to);

private:
  using ReferenceMap =
      std::unordered_map<VReg, std::vector<MachineRegisterReference>>;
  ReferenceMap definitions_;
  ReferenceMap uses_;
};

// Physical-register liveness after allocation.  Besides explicit operands it
// models call clobbers/arguments and the architectural return registers.
class MachinePhysicalRegisterLiveness {
public:
  using RegisterSet = std::unordered_set<PhysReg>;

  MachinePhysicalRegisterLiveness() = default;
  explicit MachinePhysicalRegisterLiveness(const MachineFunction &function) {
    analyze(function);
  }

  void analyze(const MachineFunction &function);
  const RegisterSet &liveIn(const MachineBasicBlock *block) const;
  const RegisterSet &liveOut(const MachineBasicBlock *block) const;
  const RegisterSet &liveBefore(const MachineInstr *instruction) const;
  const RegisterSet &liveAfter(const MachineInstr *instruction) const;
  bool isLiveBefore(const MachineInstr *instruction, PhysReg reg) const;
  bool isLiveAfter(const MachineInstr *instruction, PhysReg reg) const;

private:
  std::unordered_map<const MachineBasicBlock *, RegisterSet> liveIn_;
  std::unordered_map<const MachineBasicBlock *, RegisterSet> liveOut_;
  std::unordered_map<const MachineInstr *, RegisterSet> liveBefore_;
  std::unordered_map<const MachineInstr *, RegisterSet> liveAfter_;
};

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
