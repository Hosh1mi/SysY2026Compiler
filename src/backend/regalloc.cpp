#include "backend/regalloc.hpp"
#include "backend/interference_graph.hpp"
#include "backend/live_range_split_analysis.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

MachineOperand replacementRegister(const MachineOperand &old, PhysReg reg) {
	MachineOperand replacement =
	    MachineOperand::physReg(reg, old.regClass(), old.isDef, old.isImplicit);
	replacement.isKill = old.isKill;
	replacement.isDead = old.isDead;
	replacement.isUndef = old.isUndef;
	replacement.isEarlyClobber = old.isEarlyClobber;
	replacement.isRenamable = old.isRenamable;
	replacement.tiedTo = old.tiedTo;
	return replacement;
}

} // namespace

bool GraphColoringRegisterAllocator::colorOnce(
    MachineFunction &function, const LivenessResult &liveness,
    std::unordered_map<VReg, PhysReg> &assignments, std::vector<VReg> &spills,
    LiveRangeSplitPlans &splitPlans) const {
	splitPlans = {};
	std::unordered_map<VReg, const LiveInterval *> intervalFor;
	intervalFor.reserve(liveness.intervals.size());
	std::unordered_map<VReg, std::unordered_map<VReg, double>> affinities;
	std::unordered_map<VReg, VReg> tiedPairs; // tied use -> def
	std::unordered_map<VReg, VReg> tiedDefs;  // tied def -> use
	std::unordered_map<VReg, VReg> mandatoryTiedPairs;
	std::unordered_map<VReg, VReg> mandatoryTiedDefs;
	std::unordered_map<VReg, std::vector<PhysReg>> physicalHints;
	ForbiddenColorMap forbiddenColors;
	for (const LiveInterval &interval : liveness.intervals) {
		intervalFor.emplace(interval.reg, &interval);
	}

	InterferenceGraph graph(liveness.intervals);

	for (const auto &owned : function.blocks()) {
		for (const MachineInstr &instruction : owned->instructions()) {
			std::vector<VReg> defs;
			std::vector<VReg> uses;
			for (const MachineOperand &operand : instruction.operands()) {
				if (operand.isVirtualRegister()) {
					(operand.isDef ? defs : uses)
					    .push_back(operand.virtualRegister());
				}
			}

			VReg copyDef = 0;
			VReg copyUse = 0;
			if (instruction.opcode() == Opcode::COPY && defs.size() == 1 &&
			    uses.size() == 1) {
				copyDef = defs.front();
				copyUse = uses.front();
				double affinityWeight =
				    std::pow(10.0, std::min(owned->loopDepth, 4U));
				affinities[copyDef][copyUse] += affinityWeight;
				affinities[copyUse][copyDef] += affinityWeight;
			} else if (instruction.opcode() == Opcode::COPY &&
			           defs.size() == 1 && instruction.operands().size() >= 2 &&
			           instruction.operands()[1].isPhysicalRegister()) {
				physicalHints[defs.front()].push_back(
				    instruction.operands()[1].physicalRegister());
			} else if (instruction.opcode() == Opcode::COPY &&
			           uses.size() == 1 && !instruction.operands().empty() &&
			           instruction.operands()[0].isPhysicalRegister()) {
				physicalHints[uses.front()].push_back(
				    instruction.operands()[0].physicalRegister());
			}

			for (std::size_t i = 0; i < defs.size(); ++i)
				for (std::size_t j = i + 1; j < defs.size(); ++j)
					graph.addEdge(defs[i], defs[j]);
			// Read-only operands may legally share a physical register for
			// ordinary AArch64 instructions.  Distinctness is represented by
			// tied, early-clobber, and def/live constraints below rather than
			// by conservatively connecting every pair of uses.

			// Tied operands (e.g. fmla/fmls accumulate into vd in place):
			// the tied use must share the destination register, while the
			// destination must not collide with any other source of the
			// same instruction (vd is read-modify-write).
			for (std::size_t i = 0; i < instruction.operands().size(); ++i) {
				const MachineOperand &operand = instruction.operands()[i];
				if (!operand.isVirtualRegister() || operand.isDef ||
				    operand.tiedTo < 0 ||
				    static_cast<std::size_t>(operand.tiedTo) >=
				        instruction.operands().size())
					continue;
				const MachineOperand &defOperand =
				    instruction.operands()[operand.tiedTo];
				if (!defOperand.isVirtualRegister() || !defOperand.isDef)
					continue;
				VReg tiedDef = defOperand.virtualRegister();
				VReg tiedUse = operand.virtualRegister();
				tiedPairs[tiedUse] = tiedDef;
				tiedDefs[tiedDef] = tiedUse;
				if (operand.isKill) {
					mandatoryTiedPairs[tiedUse] = tiedDef;
					mandatoryTiedDefs[tiedDef] = tiedUse;
				}
				for (std::size_t j = 0; j < instruction.operands().size();
				     ++j) {
					const MachineOperand &other = instruction.operands()[j];
					if (other.isVirtualRegister() && !other.isDef && i != j &&
					    static_cast<std::size_t>(operand.tiedTo) != j)
						graph.addEdge(tiedDef, other.virtualRegister());
				}
			}

			for (std::size_t defIndex = 0;
			     defIndex < instruction.operands().size(); ++defIndex) {
				const MachineOperand &operand =
				    instruction.operands()[defIndex];
				if (!operand.isVirtualRegister() || !operand.isEarlyClobber ||
				    !operand.isDef)
					continue;
				for (std::size_t useIndex = 0;
				     useIndex < instruction.operands().size(); ++useIndex) {
					const MachineOperand &use =
					    instruction.operands()[useIndex];
					if (!use.isVirtualRegister() || use.isDef ||
					    use.tiedTo == static_cast<int>(defIndex))
						continue;
					graph.addEdge(operand.virtualRegister(),
					              use.virtualRegister());
				}
			}
		}
	}

	// Build def/live interference in a reverse walk.  Keeping only block
	// live-out sets avoids materializing an O(instructions × live-values)
	// table for large straight-line functions.
	for (const auto &owned : function.blocks()) {
		std::set<VReg> live;
		auto blockLive = liveness.blockLiveOut.find(owned.get());
		if (blockLive != liveness.blockLiveOut.end())
			live = blockLive->second;
		for (auto it = owned->instructions().rbegin();
		     it != owned->instructions().rend(); ++it) {
			std::vector<VReg> defs;
			std::vector<VReg> uses;
			std::vector<PhysReg> physicalDefs;
			std::vector<PhysReg> physicalUses;
			for (const MachineOperand &operand : it->operands()) {
				if (operand.isVirtualRegister())
					(operand.isDef ? defs : uses)
					    .push_back(operand.virtualRegister());
				else if (operand.isPhysicalRegister())
					(operand.isDef ? physicalDefs : physicalUses)
					    .push_back(operand.physicalRegister());
			}

			VReg copyDef = 0;
			VReg copyUse = 0;
			if (it->opcode() == Opcode::COPY && defs.size() == 1 &&
			    uses.size() == 1) {
				copyDef = defs.front();
				copyUse = uses.front();
			}
			for (VReg def : defs) {
				for (VReg liveReg : live) {
					if (def == copyDef && liveReg == copyUse)
						continue;
					graph.addEdge(def, liveReg);
				}
			}
			for (PhysReg physical : physicalDefs) {
				for (VReg liveReg : live) {
					if (it->opcode() == Opcode::COPY && liveReg == copyUse)
						continue;
					forbiddenColors[liveReg].insert(physical);
				}
			}
			// A fixed physical use also occupies its register at this
			// instruction.  This matters after pre-RA scheduling: a
			// virtual value live across `vreg = COPY phys` must not be
			// colored to phys, or it would overwrite the incoming physical
			// value before the copy.  The copy destination itself may still
			// coalesce with its physical source.
			for (PhysReg physical : physicalUses) {
				for (VReg liveReg : live) {
					if (it->opcode() == Opcode::COPY && liveReg == copyDef)
						continue;
					forbiddenColors[liveReg].insert(physical);
				}
				for (VReg used : uses)
					forbiddenColors[used].insert(physical);
			}
			for (VReg def : defs)
				live.erase(def);
			live.insert(uses.begin(), uses.end());
		}
	}

	// A tied use explicitly marked as killed can be contracted with its def:
	// mirror both neighborhoods so a yet-uncolored neighbor of one endpoint
	// cannot later take the color already chosen for the other endpoint.
	// Longer destructive chains keep the conservative repair-copy path to
	// avoid raising their effective graph degree.
	bool tiedNeighborhoodChanged = true;
	while (tiedNeighborhoodChanged) {
		tiedNeighborhoodChanged = false;
		for (const auto &[tiedUse, tiedDef] : mandatoryTiedPairs) {
			std::vector<VReg> useNeighbors;
			std::vector<VReg> defNeighbors;
			for (VReg neighbor : graph.neighbors(tiedUse))
				useNeighbors.push_back(neighbor);
			for (VReg neighbor : graph.neighbors(tiedDef))
				defNeighbors.push_back(neighbor);
			for (VReg neighbor : useNeighbors)
				tiedNeighborhoodChanged |= graph.addEdge(tiedDef, neighbor);
			for (VReg neighbor : defNeighbors)
				tiedNeighborhoodChanged |= graph.addEdge(tiedUse, neighbor);
		}
	}

	// ABI argument moves are parallel assignments represented as ordered
	// COPYs.  Precolor conflicts make that order safe even after spill code
	// is inserted between members of the group: an early destination may
	// not overwrite a physical source that has not been consumed yet.
	for (const auto &owned : function.blocks()) {
		std::unordered_map<unsigned, std::vector<const MachineInstr *>>
		    copyGroups;
		for (const MachineInstr &instruction : owned->instructions())
			if (instruction.parallelCopyGroup)
				copyGroups[instruction.parallelCopyGroup].push_back(
				    &instruction);

		for (const auto &[group, copies] : copyGroups) {
			(void)group;
			std::set<PhysReg> earlierPhysicalDefs;
			for (const MachineInstr *copy : copies) {
				if (copy->opcode() != Opcode::COPY ||
				    copy->operands().size() != 2)
					throw std::logic_error("malformed virtual parallel copy");
				const MachineOperand &destination = copy->operands()[0];
				const MachineOperand &source = copy->operands()[1];
				if (destination.isPhysicalRegister() &&
				    source.isVirtualRegister()) {
					forbiddenColors[source.virtualRegister()].insert(
					    earlierPhysicalDefs.begin(), earlierPhysicalDefs.end());
					earlierPhysicalDefs.insert(destination.physicalRegister());
				}
			}

			std::multiset<PhysReg> remainingPhysicalUses;
			for (const MachineInstr *copy : copies) {
				const MachineOperand &source = copy->operands()[1];
				if (source.isPhysicalRegister())
					remainingPhysicalUses.insert(source.physicalRegister());
			}
			for (const MachineInstr *copy : copies) {
				const MachineOperand &destination = copy->operands()[0];
				const MachineOperand &source = copy->operands()[1];
				if (source.isPhysicalRegister()) {
					auto current =
					    remainingPhysicalUses.find(source.physicalRegister());
					remainingPhysicalUses.erase(current);
				}
				if (!destination.isVirtualRegister())
					continue;
				forbiddenColors[destination.virtualRegister()].insert(
				    remainingPhysicalUses.begin(), remainingPhysicalUses.end());
			}
		}
	}

	auto colorBank = [&](bool vectorBank) {
		// Callee-saved registers have no preservation benefit in a leaf and
		// would only create prologue/epilogue traffic.  Functions containing
		// calls retain the preserved-first order so copy affinities spanning
		// call boundaries are not disrupted merely to avoid a frame save.
		const bool preferCallerSaved = !function.frameInfo().hasCalls;
		std::vector<VReg> nodes;
		for (const auto &[reg, interval] : intervalFor) {
			bool isVector = interval->regClass == RegClass::FPR32 ||
			                interval->regClass == RegClass::NEON128;
			if (isVector == vectorBank)
				nodes.push_back(reg);
		}
		std::sort(nodes.begin(), nodes.end());
		if (nodes.empty())
			return;

		std::unordered_map<VReg, unsigned> degree;
		std::unordered_map<VReg, unsigned> availableColorCount;
		std::unordered_set<VReg> remaining(nodes.begin(), nodes.end());
		std::set<VReg> lowDegree;
		for (VReg reg : nodes) {
			degree[reg] = graph.degree(reg);
			const LiveInterval &interval = *intervalFor.at(reg);
			unsigned availableColors = 0;
			for (PhysReg physical : RegisterInfo::allocationOrder(
			         interval.regClass, preferCallerSaved)) {
				if (RegisterInfo::isReserved(physical) ||
				    forbiddenColors[reg].count(physical))
					continue;
				if (interval.crossesCall &&
				    interval.regClass == RegClass::NEON128)
					continue;
				if (interval.crossesCall &&
				    RegisterInfo::isCallerSaved(physical))
					continue;
				++availableColors;
			}
			availableColorCount[reg] = availableColors;
			if (degree[reg] < availableColors)
				lowDegree.insert(reg);
		}

		std::vector<VReg> simplifyStack;
		std::unordered_set<VReg> potentialSpills;
		while (!remaining.empty()) {
			VReg selected = 0;
			if (!lowDegree.empty()) {
				selected = *lowDegree.begin();
				lowDegree.erase(lowDegree.begin());
			}

			if (!selected) {
				double bestCost = std::numeric_limits<double>::infinity();
				for (VReg reg : nodes) {
					if (!remaining.count(reg))
						continue;
					const LiveInterval &interval = *intervalFor.at(reg);
					double cost =
					    function.registerInfo().get(reg).spillTemporary
					        ? std::numeric_limits<double>::infinity()
					        : interval.weight /
					              static_cast<double>(degree[reg] + 1);
					if (cost < bestCost ||
					    (cost == bestCost && (!selected || reg < selected))) {
						bestCost = cost;
						selected = reg;
					}
				}
				potentialSpills.insert(selected);
			}

			remaining.erase(selected);
			lowDegree.erase(selected);
			simplifyStack.push_back(selected);
			for (VReg neighbor : graph.neighbors(selected)) {
				if (remaining.count(neighbor) && degree[neighbor] > 0) {
					--degree[neighbor];
					if (degree[neighbor] < availableColorCount[neighbor])
						lowDegree.insert(neighbor);
				}
			}
		}

		while (!simplifyStack.empty()) {
			VReg reg = simplifyStack.back();
			simplifyStack.pop_back();
			const LiveInterval &interval = *intervalFor.at(reg);
			std::set<PhysReg> unavailable;
			for (VReg neighbor : graph.neighbors(reg)) {
				auto assigned = assignments.find(neighbor);
				if (assigned != assignments.end())
					unavailable.insert(assigned->second);
			}
			auto allowed = [&](PhysReg physical) {
				return !RegisterInfo::isReserved(physical) &&
				       !unavailable.count(physical) &&
				       !forbiddenColors[reg].count(physical) &&
				       !(interval.crossesCall &&
				         interval.regClass == RegClass::NEON128) &&
				       (!interval.crossesCall ||
				        !RegisterInfo::isCallerSaved(physical));
			};

			PhysReg selected = PhysReg::NoReg;
			VReg tiedPartner = 0;
			auto tiedUse = tiedPairs.find(reg);
			if (tiedUse != tiedPairs.end())
				tiedPartner = tiedUse->second;
			auto tiedDef = tiedDefs.find(reg);
			if (!tiedPartner && tiedDef != tiedDefs.end())
				tiedPartner = tiedDef->second;
			bool mandatoryTie = false;
			auto mandatoryUse = mandatoryTiedPairs.find(reg);
			if (mandatoryUse != mandatoryTiedPairs.end()) {
				tiedPartner = mandatoryUse->second;
				mandatoryTie = true;
			}
			auto mandatoryDef = mandatoryTiedDefs.find(reg);
			if (mandatoryDef != mandatoryTiedDefs.end()) {
				tiedPartner = mandatoryDef->second;
				mandatoryTie = true;
			}

			auto partnerAllows = [&](PhysReg physical) {
				if (!mandatoryTie || !tiedPartner ||
				    assignments.count(tiedPartner))
					return true;
				const LiveInterval &partnerInterval =
				    *intervalFor.at(tiedPartner);
				if (RegisterInfo::isReserved(physical) ||
				    forbiddenColors[tiedPartner].count(physical) ||
				    (partnerInterval.crossesCall &&
				     partnerInterval.regClass == RegClass::NEON128) ||
				    (partnerInterval.crossesCall &&
				     RegisterInfo::isCallerSaved(physical)))
					return false;
				bool conflict = false;
				for (VReg neighbor : graph.neighbors(tiedPartner)) {
					auto assigned = assignments.find(neighbor);
					if (assigned != assignments.end() &&
					    assigned->second == physical)
						conflict = true;
				}
				return !conflict;
			};
			auto jointlyAllowed = [&](PhysReg physical) {
				return allowed(physical) && partnerAllows(physical);
			};

			// Tied pairs must share a register: prefer the partner's color
			// when it is assigned.  If it is still pending, every candidate
			// must also be legal for that partner so later coloring cannot
			// split the destructive instruction and require a repair copy.
			if (tiedPartner) {
				auto assigned = assignments.find(tiedPartner);
				if (assigned != assignments.end() && allowed(assigned->second))
					selected = assigned->second;
			}
			auto hints = physicalHints.find(reg);
			if (hints != physicalHints.end() &&
			    (selected == PhysReg::NoReg || !mandatoryTie)) {
				for (PhysReg hint : hints->second)
					if (jointlyAllowed(hint)) {
						selected = hint;
						break;
					}
			}
			if (selected == PhysReg::NoReg) {
				std::vector<std::pair<VReg, double>> weightedAffinities;
				auto regAffinities = affinities.find(reg);
				if (regAffinities != affinities.end())
					weightedAffinities.assign(regAffinities->second.begin(),
					                          regAffinities->second.end());
				std::sort(weightedAffinities.begin(), weightedAffinities.end(),
				          [](const auto &lhs, const auto &rhs) {
					          return lhs.second != rhs.second
					                     ? lhs.second > rhs.second
					                     : lhs.first < rhs.first;
				          });
				for (const auto &[affinity, weight] : weightedAffinities) {
					(void)weight;
					auto assigned = assignments.find(affinity);
					if (assigned != assignments.end() &&
					    jointlyAllowed(assigned->second)) {
						selected = assigned->second;
						break;
					}
				}
			}
			if (selected == PhysReg::NoReg) {
				for (PhysReg candidate : RegisterInfo::allocationOrder(
				         interval.regClass, preferCallerSaved))
					if (jointlyAllowed(candidate)) {
						selected = candidate;
						break;
					}
			}
			if (selected == PhysReg::NoReg)
				spills.push_back(reg);
			else
				assignments.emplace(reg, selected);
		}
	};

	colorBank(false);
	colorBank(true);
	std::sort(spills.begin(), spills.end());
	spills.erase(std::unique(spills.begin(), spills.end()), spills.end());
	if (!spills.empty()) {
		LiveRangeSplitAnalysis splitAnalysis;
		splitPlans = splitAnalysis.analyze(
		    function, liveness, graph, assignments, spills, forbiddenColors);
		return false;
	}

	// Optimistic recoloring completes the graph-coloring copy-coalescing
	// step.  It joins a non-interfering COPY pair whenever one endpoint can
	// adopt the other's color without changing any neighbor assignment.
	struct AffinityEdge {
		VReg lhs = 0;
		VReg rhs = 0;
		double weight = 0.0;
	};
	std::vector<AffinityEdge> affinityEdges;
	for (const auto &[reg, partners] : affinities)
		for (const auto &[partner, weight] : partners)
			if (reg < partner)
				affinityEdges.push_back(AffinityEdge{reg, partner, weight});
	std::sort(affinityEdges.begin(), affinityEdges.end(),
	          [](const AffinityEdge &lhs, const AffinityEdge &rhs) {
		          if (lhs.weight != rhs.weight)
			          return lhs.weight > rhs.weight;
		          if (lhs.lhs != rhs.lhs)
			          return lhs.lhs < rhs.lhs;
		          return lhs.rhs < rhs.rhs;
	          });
	auto strongestSatisfiedAffinity = [&](VReg reg) {
		double weight = 0.0;
		auto assigned = assignments.find(reg);
		if (assigned == assignments.end())
			return weight;
		auto regAffinities = affinities.find(reg);
		if (regAffinities == affinities.end())
			return weight;
		for (const auto &[partner, candidateWeight] : regAffinities->second) {
			auto partnerAssignment = assignments.find(partner);
			if (partnerAssignment != assignments.end() &&
			    partnerAssignment->second == assigned->second)
				weight = std::max(weight, candidateWeight);
		}
		return weight;
	};

	for (unsigned iteration = 0; iteration < 4; ++iteration) {
		bool changed = false;
		for (const AffinityEdge &edge : affinityEdges) {
			VReg reg = edge.lhs;
			VReg partner = edge.rhs;
			if (graph.hasEdge(reg, partner) || !assignments.count(reg) ||
			    !assignments.count(partner) ||
			    assignments[reg] == assignments[partner])
				continue;
			auto canRecolor = [&](VReg value, PhysReg color) {
				const LiveInterval &interval = *intervalFor.at(value);
				if (RegisterInfo::isReserved(color) ||
				    forbiddenColors[value].count(color) ||
				    (interval.crossesCall &&
				     interval.regClass == RegClass::NEON128) ||
				    (interval.crossesCall &&
				     RegisterInfo::isCallerSaved(color)))
					return false;
				bool conflict = false;
				for (VReg neighbor : graph.neighbors(value)) {
					if (assignments.count(neighbor) &&
					    assignments[neighbor] == color)
						conflict = true;
				}
				return !conflict;
			};
			if (edge.weight >= strongestSatisfiedAffinity(reg) &&
			    canRecolor(reg, assignments[partner])) {
				assignments[reg] = assignments[partner];
				changed = true;
			} else if (edge.weight >= strongestSatisfiedAffinity(partner) &&
			           canRecolor(partner, assignments[reg])) {
				assignments[partner] = assignments[reg];
				changed = true;
			}
		}
		if (!changed)
			break;
	}
	return true;
}

void GraphColoringRegisterAllocator::rewriteVirtualRegisters(
    MachineFunction &function,
    const std::unordered_map<VReg, PhysReg> &assignments) const {
	for (auto &owned : function.blocks()) {
		for (MachineInstr &instruction : owned->instructions()) {
			for (MachineOperand &operand : instruction.operands()) {
				if (!operand.isVirtualRegister())
					continue;
				auto assignment = assignments.find(operand.virtualRegister());
				if (assignment == assignments.end())
					throw std::logic_error(
					    "register allocator left an unassigned vreg");
				operand = replacementRegister(operand, assignment->second);
			}
		}
	}
	function.setProperty(MachineProperty::NoVRegs);
	function.clearProperty(MachineProperty::IsSSA);
	function.clearProperty(MachineProperty::HasPHIs);
	function.clearProperty(MachineProperty::TracksLiveness);
}

void GraphColoringRegisterAllocator::run(MachineFunction &function) const {
	MachineLiveness analysis;
	LiveRangeEdit liveRangeEdit;
	std::unordered_map<VReg, int> spillSlots;
	double committedSplitCost = 0.0;
	double bestRepairObjective = std::numeric_limits<double>::infinity();
	constexpr unsigned kMaximumAllocationRounds = 64;
	for (unsigned round = 0; round < kMaximumAllocationRounds; ++round) {
		// Spill and split rewrites do not change the CFG, so loop depth is
		// invariant across allocation rounds.
		LivenessResult liveness = analysis.run(function, round == 0);
		std::unordered_map<VReg, PhysReg> assignments;
		std::vector<VReg> spills;
		LiveRangeSplitPlans splitPlans;
		if (colorOnce(function, liveness, assignments, spills, splitPlans)) {
			rewriteVirtualRegisters(function, assignments);
			return;
		}

		double unresolvedSpillCost = 0.0;
		for (VReg spill : spills) {
			const LiveInterval *interval = liveness.find(spill);
			if (interval)
				unresolvedSpillCost += interval->spillCost;
		}
		const double repairObjective = unresolvedSpillCost + committedSplitCost;
		const bool repairMadeProgress = repairObjective < bestRepairObjective;

		bool splitApplied = false;
		if (repairMadeProgress) {
			bestRepairObjective = repairObjective;
			for (const LocalSplitPlan &splitPlan : splitPlans.local) {
				if (!liveRangeEdit.splitLocalGap(function, liveness, splitPlan,
				                                 spillSlots))
					continue;
				committedSplitCost += splitPlan.estimatedCost;
				splitApplied = true;
			}
		}
		if (splitApplied)
			continue;
		insertSpills(function, spills, spillSlots);
		// Spilling creates a different interference problem.  A later repair
		// sequence starts with a fresh objective instead of comparing against
		// the completed sequence that led to this rewrite.
		committedSplitCost = 0.0;
		bestRepairObjective = std::numeric_limits<double>::infinity();
	}
	throw std::logic_error(
	    "register allocation exceeded the bounded spill iteration limit");
}

} // namespace backend::aarch64
