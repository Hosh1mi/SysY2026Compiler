// This file computes MachineFunction dominance and natural-loop information
// once in a reusable form instead of embedding subtly different algorithms in
// individual transformations.
#include "backend/machine_analysis.hpp"

#include <algorithm>
#include <utility>

namespace backend::aarch64 {

MachineOperand &MachineRegisterReference::operand() const {
  return instruction->operands().at(operandIndex);
}

void MachineRegisterIndex::rebuild(MachineFunction &function) {
  definitions_.clear();
  uses_.clear();
  for (const auto &owned : function.blocks()) {
    MachineBasicBlock *block = owned.get();
    for (MachineInstr &instruction : block->instructions()) {
      for (unsigned index = 0; index < instruction.operands().size(); ++index) {
        MachineOperand &operand = instruction.operands()[index];
        if (!operand.isVirtualRegister())
          continue;
        MachineRegisterReference reference{block, &instruction, index};
        (operand.isDef ? definitions_ : uses_)
            [operand.virtualRegister()].push_back(reference);
      }
    }
  }
}

const std::vector<MachineRegisterReference> &
MachineRegisterIndex::definitions(VReg reg) const {
  static const std::vector<MachineRegisterReference> empty;
  auto found = definitions_.find(reg);
  return found == definitions_.end() ? empty : found->second;
}

const std::vector<MachineRegisterReference> &
MachineRegisterIndex::uses(VReg reg) const {
  static const std::vector<MachineRegisterReference> empty;
  auto found = uses_.find(reg);
  return found == uses_.end() ? empty : found->second;
}

MachineInstr *MachineRegisterIndex::uniqueDefinition(VReg reg) const {
  const auto &references = definitions(reg);
  return references.size() == 1 ? references.front().instruction : nullptr;
}

MachineBasicBlock *
MachineRegisterIndex::uniqueDefinitionBlock(VReg reg) const {
  const auto &references = definitions(reg);
  return references.size() == 1 ? references.front().block : nullptr;
}

unsigned MachineRegisterIndex::useCount(VReg reg) const {
  return static_cast<unsigned>(uses(reg).size());
}

bool MachineRegisterIndex::allUsesHaveOpcode(VReg reg, Opcode opcode) const {
  const auto &references = uses(reg);
  if (references.empty())
    return false;
  return std::all_of(references.begin(), references.end(),
                     [opcode](const MachineRegisterReference &reference) {
                       return reference.instruction->opcode() == opcode;
                     });
}

unsigned MachineRegisterIndex::replaceUses(VReg from, VReg to) {
  if (from == to)
    return 0;
  auto found = uses_.find(from);
  if (found == uses_.end())
    return 0;

  std::vector<MachineRegisterReference> references = std::move(found->second);
  uses_.erase(found);
  auto &replacementUses = uses_[to];
  replacementUses.reserve(replacementUses.size() + references.size());
  for (MachineRegisterReference &reference : references) {
    reference.operand().replaceVirtualRegister(to);
    replacementUses.push_back(reference);
  }
  return static_cast<unsigned>(references.size());
}

namespace {

using PhysSet = MachinePhysicalRegisterLiveness::RegisterSet;

void collectPhysicalUsesAndDefs(const MachineInstr &instruction,
                                PhysSet &uses, PhysSet &defs) {
  for (const MachineOperand &operand : instruction.operands()) {
    if (operand.isPhysicalRegister()) {
      (operand.isDef ? defs : uses).insert(operand.physicalRegister());
      continue;
    }
    if (operand.kind() != MachineOperand::Kind::RegisterMask)
      continue;
    for (unsigned raw = static_cast<unsigned>(PhysReg::X0);
         raw <= static_cast<unsigned>(PhysReg::V31); ++raw) {
      PhysReg reg = static_cast<PhysReg>(raw);
      if (reg == PhysReg::SP || reg == PhysReg::XZR)
        continue;
      if (!operand.registerMask().preserves(reg))
        defs.insert(reg);
    }
  }

  if (instruction.isCall()) {
    for (unsigned index = 0; index < 8; ++index) {
      uses.insert(RegisterInfo::integerArgumentRegister(index));
      uses.insert(RegisterInfo::vectorArgumentRegister(index));
    }
  } else if (instruction.opcode() == Opcode::RET) {
    uses.insert(PhysReg::X0);
    uses.insert(PhysReg::V0);
    uses.insert(PhysReg::X30);
  }
}

const PhysSet &lookupPhysicalSet(
    const std::unordered_map<const MachineBasicBlock *, PhysSet> &sets,
    const MachineBasicBlock *block) {
  static const PhysSet empty;
  auto found = sets.find(block);
  return found == sets.end() ? empty : found->second;
}

const PhysSet &lookupPhysicalSet(
    const std::unordered_map<const MachineInstr *, PhysSet> &sets,
    const MachineInstr *instruction) {
  static const PhysSet empty;
  auto found = sets.find(instruction);
  return found == sets.end() ? empty : found->second;
}

} // namespace

void MachinePhysicalRegisterLiveness::analyze(
    const MachineFunction &function) {
  liveIn_.clear();
  liveOut_.clear();
  liveBefore_.clear();
  liveAfter_.clear();

  std::unordered_map<const MachineBasicBlock *, RegisterSet> blockUses;
  std::unordered_map<const MachineBasicBlock *, RegisterSet> blockDefs;
  for (const auto &owned : function.blocks()) {
    const MachineBasicBlock *block = owned.get();
    for (const MachineInstr &instruction : block->instructions()) {
      RegisterSet uses;
      RegisterSet defs;
      collectPhysicalUsesAndDefs(instruction, uses, defs);
      for (PhysReg reg : uses)
        if (!blockDefs[block].count(reg))
          blockUses[block].insert(reg);
      blockDefs[block].insert(defs.begin(), defs.end());
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (auto owned = function.blocks().rbegin();
         owned != function.blocks().rend(); ++owned) {
      const MachineBasicBlock *block = owned->get();
      RegisterSet nextOut;
      for (const MachineBasicBlock *successor : block->successors()) {
        const RegisterSet &successorLiveIn = liveIn_[successor];
        nextOut.insert(successorLiveIn.begin(), successorLiveIn.end());
      }
      RegisterSet nextIn = blockUses[block];
      for (PhysReg reg : nextOut)
        if (!blockDefs[block].count(reg))
          nextIn.insert(reg);
      if (nextIn != liveIn_[block] || nextOut != liveOut_[block]) {
        liveIn_[block] = std::move(nextIn);
        liveOut_[block] = std::move(nextOut);
        changed = true;
      }
    }
  }

  for (const auto &owned : function.blocks()) {
    RegisterSet live = liveOut_[owned.get()];
    for (auto instruction = owned->instructions().rbegin();
         instruction != owned->instructions().rend(); ++instruction) {
      liveAfter_[&*instruction] = live;
      RegisterSet uses;
      RegisterSet defs;
      collectPhysicalUsesAndDefs(*instruction, uses, defs);
      for (PhysReg reg : defs)
        live.erase(reg);
      live.insert(uses.begin(), uses.end());
      liveBefore_[&*instruction] = live;
    }
  }
}

const MachinePhysicalRegisterLiveness::RegisterSet &
MachinePhysicalRegisterLiveness::liveIn(
    const MachineBasicBlock *block) const {
  return lookupPhysicalSet(liveIn_, block);
}

const MachinePhysicalRegisterLiveness::RegisterSet &
MachinePhysicalRegisterLiveness::liveOut(
    const MachineBasicBlock *block) const {
  return lookupPhysicalSet(liveOut_, block);
}

const MachinePhysicalRegisterLiveness::RegisterSet &
MachinePhysicalRegisterLiveness::liveBefore(
    const MachineInstr *instruction) const {
  return lookupPhysicalSet(liveBefore_, instruction);
}

const MachinePhysicalRegisterLiveness::RegisterSet &
MachinePhysicalRegisterLiveness::liveAfter(
    const MachineInstr *instruction) const {
  return lookupPhysicalSet(liveAfter_, instruction);
}

bool MachinePhysicalRegisterLiveness::isLiveBefore(
    const MachineInstr *instruction, PhysReg reg) const {
  return liveBefore(instruction).count(reg) != 0;
}

bool MachinePhysicalRegisterLiveness::isLiveAfter(
    const MachineInstr *instruction, PhysReg reg) const {
  return liveAfter(instruction).count(reg) != 0;
}

void MachineDominatorTree::analyze(const MachineFunction &function) {
  reachable_.clear();
  dominators_.clear();

  std::vector<const MachineBasicBlock *> blocks;
  blocks.reserve(function.blocks().size());
  for (const auto &block : function.blocks())
    blocks.push_back(block.get());
  if (blocks.empty())
    return;

  std::vector<const MachineBasicBlock *> worklist = {function.entryBlock()};
  while (!worklist.empty()) {
    const MachineBasicBlock *block = worklist.back();
    worklist.pop_back();
    if (!block || !reachable_.insert(block).second)
      continue;
    worklist.insert(worklist.end(), block->successors().begin(),
                    block->successors().end());
  }

  for (const MachineBasicBlock *block : blocks) {
    if (!reachable_.count(block) || block == function.entryBlock())
      dominators_[block].insert(block);
    else
      dominators_[block].insert(reachable_.begin(), reachable_.end());
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const MachineBasicBlock *block : blocks) {
      if (!reachable_.count(block) || block == function.entryBlock())
        continue;

      BlockSet next;
      bool first = true;
      for (const MachineBasicBlock *predecessor : block->predecessors()) {
        if (!reachable_.count(predecessor))
          continue;
        if (first) {
          next = dominators_.at(predecessor);
          first = false;
          continue;
        }
        for (auto it = next.begin(); it != next.end();) {
          if (!dominators_.at(predecessor).count(*it))
            it = next.erase(it);
          else
            ++it;
        }
      }
      next.insert(block);
      if (next != dominators_.at(block)) {
        dominators_[block] = std::move(next);
        changed = true;
      }
    }
  }
}

bool MachineDominatorTree::isReachable(const MachineBasicBlock *block) const {
  return reachable_.count(block) != 0;
}

bool MachineDominatorTree::dominates(const MachineBasicBlock *dominator,
                                     const MachineBasicBlock *block) const {
  auto found = dominators_.find(block);
  return found != dominators_.end() && found->second.count(dominator) != 0;
}

void MachineLoopInfo::analyze(MachineFunction &function,
                              const MachineDominatorTree &dominators) {
  loops_.clear();
  depths_.clear();

  std::unordered_map<MachineBasicBlock *, std::size_t> loopForHeader;
  for (const auto &owned : function.blocks()) {
    MachineBasicBlock *tail = owned.get();
    if (!dominators.isReachable(tail))
      continue;
    for (MachineBasicBlock *header : tail->successors()) {
      if (!dominators.dominates(header, tail))
        continue;

      std::size_t index = 0;
      auto found = loopForHeader.find(header);
      if (found == loopForHeader.end()) {
        index = loops_.size();
        loopForHeader.emplace(header, index);
        loops_.push_back(MachineLoop{header, {header}});
      } else {
        index = found->second;
      }

      MachineLoop &loop = loops_[index];
      if (!loop.blocks.insert(tail).second)
        continue;
      std::vector<MachineBasicBlock *> worklist = {tail};
      while (!worklist.empty()) {
        MachineBasicBlock *current = worklist.back();
        worklist.pop_back();
        for (MachineBasicBlock *predecessor : current->predecessors())
          if (dominators.isReachable(predecessor) &&
              loop.blocks.insert(predecessor).second && predecessor != header)
            worklist.push_back(predecessor);
      }
    }
  }

  for (const MachineLoop &loop : loops_)
    for (MachineBasicBlock *block : loop.blocks)
      ++depths_[block];
  for (const auto &owned : function.blocks())
    owned->loopDepth = depth(owned.get());
}

unsigned MachineLoopInfo::depth(const MachineBasicBlock *block) const {
  auto found = depths_.find(block);
  return found == depths_.end() ? 0 : found->second;
}

} // namespace backend::aarch64
