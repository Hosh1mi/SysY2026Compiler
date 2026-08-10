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
