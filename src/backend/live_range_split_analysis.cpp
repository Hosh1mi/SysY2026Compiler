// This file selects profitable local live-range cuts from failed graph-
// coloring decisions, using target costs rather than size heuristics.
#include "backend/live_range_split_analysis.hpp"

#include "backend/machine_analysis.hpp"
#include "backend/rematerialization.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

double blockWeight(const MachineBasicBlock &block) {
  return std::pow(10.0, std::min(block.loopDepth, 4U));
}

bool colorIsLegal(const LiveInterval &interval, PhysReg color,
                  const ForbiddenColorMap &forbiddenColors) {
  if (RegisterInfo::isReserved(color))
    return false;
  auto forbidden = forbiddenColors.find(interval.reg);
  if (forbidden != forbiddenColors.end() &&
      forbidden->second.count(color))
    return false;
  if (interval.crossesCall && interval.regClass == RegClass::NEON128)
    return false;
  return !interval.crossesCall || !RegisterInfo::isCallerSaved(color);
}

bool isSoleColorBlocker(
    VReg spill, VReg blocker, PhysReg color,
    const InterferenceGraph &graph,
    const std::unordered_map<VReg, PhysReg> &assignments) {
  for (VReg neighbor : graph.neighbors(spill)) {
    auto assigned = assignments.find(neighbor);
    if (neighbor != blocker && assigned != assignments.end() &&
        assigned->second == color)
      return false;
  }
  return true;
}

std::vector<VReg> independentSpills(
    std::vector<VReg> candidates, const InterferenceGraph &graph,
    const std::unordered_map<VReg, LiveInterval> &intervalFor) {
  std::sort(candidates.begin(), candidates.end(),
            [&intervalFor](VReg lhs, VReg rhs) {
              const double lhsCost = intervalFor.at(lhs).spillCost;
              const double rhsCost = intervalFor.at(rhs).spillCost;
              return lhsCost != rhsCost ? lhsCost > rhsCost : lhs < rhs;
            });
  std::vector<VReg> independent;
  for (VReg candidate : candidates) {
    bool interferes = false;
    for (VReg selected : independent)
      interferes |= graph.hasEdge(candidate, selected);
    if (!interferes)
      independent.push_back(candidate);
  }
  return independent;
}

double spillBenefit(
    const std::vector<VReg> &spills,
    const std::unordered_map<VReg, LiveInterval> &intervalFor) {
  double benefit = 0.0;
  for (VReg spill : spills)
    benefit += intervalFor.at(spill).spillCost;
  return benefit;
}

bool intervalInside(const LiveInterval &interval,
                    MachineSlotRange range) {
  if (interval.segments.empty())
    return false;
  for (const MachineSlotRange &segment : interval.segments)
    if (segment.begin < range.begin || segment.end > range.end)
      return false;
  return true;
}

} // namespace

LiveRangeSplitPlans LiveRangeSplitAnalysis::analyze(
    MachineFunction &function, const LivenessResult &liveness,
    const InterferenceGraph &graph,
    const std::unordered_map<VReg, PhysReg> &assignments,
    const std::vector<VReg> &spills,
    const ForbiddenColorMap &forbiddenColors) const {
  LiveRangeSplitPlans result;
  if (spills.empty())
    return result;

  std::unordered_map<VReg, LiveInterval> intervalFor;
  for (const LiveInterval &interval : liveness.intervals)
    intervalFor.emplace(interval.reg, interval);

  std::vector<VReg> assignedRegs;
  assignedRegs.reserve(assignments.size());
  for (const auto &[reg, physical] : assignments) {
    (void)physical;
    assignedRegs.push_back(reg);
  }
  MachineRegisterIndex registers(function);
  RematerializationAnalysis rematerializationAnalysis;
  RematerializationAnalysis::RecipeMap rematerializations =
      rematerializationAnalysis.analyze(function, registers, assignedRegs);

  std::unordered_map<VReg, LocalSplitPlan> bestLocal;
  for (const auto &[blocker, color] : assignments) {
    const LiveInterval &blockerInterval = intervalFor.at(blocker);
    const VRegInfo &blockerInfo = function.registerInfo().get(blocker);
    if (blockerInfo.spillTemporary || blockerInfo.splitGeneration != 0)
      continue;

    const auto rematerialization = rematerializations.find(blocker);
    const bool canRematerialize =
        rematerialization != rematerializations.end();

    // A local gap splits into a sibling, so it currently requires one
    // definition and a single block.
    if (blockerInterval.operands.size() < 2)
      continue;
    MachineBasicBlock *block = blockerInterval.operands.front().block;
    unsigned definitions = 0;
    bool oneBlock = block != nullptr;
    for (const LiveRangeOperand &operand : blockerInterval.operands) {
      oneBlock &= operand.block == block;
      definitions += operand.isDef;
    }
    if (!oneBlock || definitions != 1 ||
        !blockerInterval.operands.front().isDef)
      continue;

    const double boundaryCost =
        blockWeight(*block) *
        (canRematerialize
             ? rematerialization->second.cost
             : InstrInfo::get(Opcode::SPILL_STORE).latency +
                   InstrInfo::get(Opcode::SPILL_LOAD).latency);
    for (std::size_t gapIndex = 0;
         gapIndex + 1 < blockerInterval.operands.size(); ++gapIndex) {
      const LiveRangeOperand &left = blockerInterval.operands[gapIndex];
      const LiveRangeOperand &right = blockerInterval.operands[gapIndex + 1];
      if (left.instruction == right.instruction || right.isDef)
        continue;
      if (canRematerialize) {
        bool hasLeftUse = false;
        for (std::size_t index = 0; index <= gapIndex; ++index)
          hasLeftUse |= !blockerInterval.operands[index].isDef;
        if (!hasLeftUse)
          continue;
      }

      const MachineSlot gapBegin =
          liveness.slots.deadSlot(*left.instruction) + 1;
      const MachineSlot gapEnd =
          liveness.slots.earlyDefSlot(*right.instruction);
      if (gapBegin >= gapEnd)
        continue;

      std::vector<VReg> helped;
      const MachineSlotRange gap{gapBegin, gapEnd};
      for (VReg spill : spills) {
        const LiveInterval &spillInterval = intervalFor.at(spill);
        if (!intervalInside(spillInterval, gap) ||
            !graph.hasEdge(blocker, spill) ||
            !colorIsLegal(spillInterval, color, forbiddenColors) ||
            !isSoleColorBlocker(spill, blocker, color, graph, assignments))
          continue;
        helped.push_back(spill);
      }
      helped = independentSpills(std::move(helped), graph, intervalFor);
      const double benefit = spillBenefit(helped, intervalFor);
      const double netBenefit = benefit - boundaryCost;
      auto previous = bestLocal.find(blocker);
      const double previousNet =
          previous == bestLocal.end()
              ? 0.0
              : previous->second.estimatedBenefit -
                    previous->second.estimatedCost;
      if (netBenefit > previousNet)
        bestLocal[blocker] = LocalSplitPlan{
            blocker, block, left.instruction, right.instruction, right.slot,
            benefit, boundaryCost, std::move(helped)};
    }
  }

  std::vector<LocalSplitPlan> localCandidates;
  localCandidates.reserve(bestLocal.size());
  for (auto &[reg, plan] : bestLocal) {
    (void)reg;
    localCandidates.push_back(std::move(plan));
  }
  std::sort(localCandidates.begin(), localCandidates.end(),
            [](const LocalSplitPlan &lhs, const LocalSplitPlan &rhs) {
              const double lhsNet =
                  lhs.estimatedBenefit - lhs.estimatedCost;
              const double rhsNet =
                  rhs.estimatedBenefit - rhs.estimatedCost;
              return lhsNet != rhsNet ? lhsNet > rhsNet
                                      : lhs.parent < rhs.parent;
            });

  // A failed range may be repairable by several blockers.  Charge its saved
  // spill cost once across all cuts selected in this round.
  std::unordered_set<VReg> relieved;
  for (LocalSplitPlan &plan : localCandidates) {
    std::vector<VReg> marginalSpills;
    for (VReg spill : plan.relievedSpills)
      if (!relieved.count(spill))
        marginalSpills.push_back(spill);
    const double marginalBenefit =
        spillBenefit(marginalSpills, intervalFor);
    if (marginalBenefit <= plan.estimatedCost)
      continue;
    plan.estimatedBenefit = marginalBenefit;
    plan.relievedSpills = std::move(marginalSpills);
    relieved.insert(plan.relievedSpills.begin(),
                    plan.relievedSpills.end());
    result.local.push_back(std::move(plan));
  }
  return result;
}

} // namespace backend::aarch64
