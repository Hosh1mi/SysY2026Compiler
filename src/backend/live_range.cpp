// This file numbers Machine instructions and computes precise live-range
// segments and operand positions for register-allocation transformations.
#include "backend/live_range.hpp"

#include "backend/machine_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace backend::aarch64 {
namespace {

using RegSet = std::set<VReg>;

struct MutableInterval {
	double weight = 0.0;
	double spillCost = 0.0;
	std::vector<MachineSlotRange> segments;
	std::vector<LiveRangeOperand> operands;
};

struct MutableSegment {
	MachineSlot begin = std::numeric_limits<MachineSlot>::max();
	MachineSlot end = 0;

	void include(MachineSlot slot) {
		begin = std::min(begin, slot);
		end = std::max(end, slot + 1);
	}
};

MachineSlot operandSlot(const MachineSlotIndexes &slots,
                        const MachineInstr &instruction,
                        const MachineOperand &operand) {
	if (!operand.isDef)
		return slots.useSlot(instruction);
	if (operand.isEarlyClobber)
		return slots.earlyDefSlot(instruction);
	return slots.defSlot(instruction);
}

void mergeSegments(std::vector<MachineSlotRange> &segments) {
	std::sort(segments.begin(), segments.end(),
	          [](const MachineSlotRange &lhs, const MachineSlotRange &rhs) {
		          if (lhs.begin != rhs.begin)
			          return lhs.begin < rhs.begin;
		          return lhs.end < rhs.end;
	          });
	std::vector<MachineSlotRange> merged;
	merged.reserve(segments.size());
	for (const MachineSlotRange &segment : segments) {
		if (merged.empty() || merged.back().end < segment.begin) {
			merged.push_back(segment);
			continue;
		}
		merged.back().end = std::max(merged.back().end, segment.end);
	}
	segments = std::move(merged);
}

} // namespace

void MachineSlotIndexes::number(MachineFunction &function) {
	blockRanges_.clear();
	MachineSlot slot = kInstructionSpacing;
	for (const auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		const MachineSlot blockBegin = slot;
		for (MachineInstr &instruction : block->instructions()) {
			instruction.slotIndex = slot;
			slot += kInstructionSpacing;
		}
		const MachineSlot blockEnd = std::max(blockBegin + 1, slot);
		blockRanges_.emplace(block, MachineSlotRange{blockBegin, blockEnd});
		slot += kInstructionSpacing;
	}
}

MachineSlot
MachineSlotIndexes::earlyDefSlot(const MachineInstr &instruction) const {
	return instruction.slotIndex;
}

MachineSlot MachineSlotIndexes::useSlot(const MachineInstr &instruction) const {
	return instruction.slotIndex + 1;
}

MachineSlot MachineSlotIndexes::defSlot(const MachineInstr &instruction) const {
	return instruction.slotIndex + 2;
}

MachineSlot
MachineSlotIndexes::deadSlot(const MachineInstr &instruction) const {
	return instruction.slotIndex + 3;
}

MachineSlotRange
MachineSlotIndexes::blockRange(const MachineBasicBlock *block) const {
	auto found = blockRanges_.find(block);
	if (found == blockRanges_.end())
		throw std::logic_error("missing Machine block slot range");
	return found->second;
}

MachineOperand &LiveRangeOperand::operand() const {
	return instruction->operands().at(operandIndex);
}

bool LiveInterval::overlaps(const LiveInterval &other) const {
	std::size_t lhs = 0;
	std::size_t rhs = 0;
	while (lhs < segments.size() && rhs < other.segments.size()) {
		if (segments[lhs].overlaps(other.segments[rhs]))
			return true;
		if (segments[lhs].end <= other.segments[rhs].begin)
			++lhs;
		else
			++rhs;
	}
	return false;
}

bool LiveInterval::liveAt(MachineSlot slot) const {
	auto found = std::upper_bound(
	    segments.begin(), segments.end(), slot,
	    [](MachineSlot position, const MachineSlotRange &segment) {
		    return position < segment.begin;
	    });
	return found != segments.begin() && std::prev(found)->contains(slot);
}

const LiveInterval *LivenessResult::find(VReg reg) const {
	auto found = intervalIndex.find(reg);
	return found == intervalIndex.end() ? nullptr : &intervals[found->second];
}

LivenessResult MachineLiveness::run(MachineFunction &function,
                                    bool refreshLoopInfo) const {
	if (refreshLoopInfo) {
		MachineDominatorTree dominators;
		dominators.analyze(function);
		MachineLoopInfo loops;
		loops.analyze(function, dominators);
	}

	LivenessResult result;
	result.slots.number(function);

	std::unordered_map<MachineBasicBlock *, RegSet> uses;
	std::unordered_map<MachineBasicBlock *, RegSet> defs;
	for (const auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		for (const MachineInstr &instruction : block->instructions()) {
			for (const MachineOperand &operand : instruction.operands()) {
				if (!operand.isVirtualRegister())
					continue;
				VReg reg = operand.virtualRegister();
				if (operand.isDef)
					defs[block].insert(reg);
				else if (!defs[block].count(reg))
					uses[block].insert(reg);
			}
		}
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (auto blockIt = function.blocks().rbegin();
		     blockIt != function.blocks().rend(); ++blockIt) {
			MachineBasicBlock *block = blockIt->get();
			RegSet newOut;
			for (MachineBasicBlock *successor : block->successors())
				newOut.insert(result.blockLiveIn[successor].begin(),
				              result.blockLiveIn[successor].end());
			RegSet newIn = uses[block];
			for (VReg reg : newOut)
				if (!defs[block].count(reg))
					newIn.insert(reg);
			if (newOut != result.blockLiveOut[block] ||
			    newIn != result.blockLiveIn[block]) {
				result.blockLiveOut[block] = std::move(newOut);
				result.blockLiveIn[block] = std::move(newIn);
				changed = true;
			}
		}
	}

	std::unordered_set<VReg> liveAcrossCalls;
	for (const auto &owned : function.blocks()) {
		RegSet live = result.blockLiveOut[owned.get()];
		for (auto it = owned->instructions().rbegin();
		     it != owned->instructions().rend(); ++it) {
			if (it->isCall())
				liveAcrossCalls.insert(live.begin(), live.end());
			for (const MachineOperand &operand : it->operands())
				if (operand.isVirtualRegister() && operand.isDef)
					live.erase(operand.virtualRegister());
			for (const MachineOperand &operand : it->operands())
				if (operand.isVirtualRegister() && !operand.isDef)
					live.insert(operand.virtualRegister());
		}
	}

	std::unordered_map<VReg, MutableInterval> mutableIntervals;
	for (const auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		const MachineSlotRange blockSlots = result.slots.blockRange(block);
		std::unordered_map<VReg, MutableSegment> blockSegments;
		for (VReg reg : result.blockLiveIn[block])
			blockSegments[reg].include(blockSlots.begin);

		const double blockWeight =
		    std::pow(10.0, std::min(block->loopDepth, 4U));
		const double loadCost = InstrInfo::get(Opcode::SPILL_LOAD).latency;
		const double storeCost = InstrInfo::get(Opcode::SPILL_STORE).latency;
		for (MachineInstr &instruction : block->instructions()) {
			for (unsigned index = 0; index < instruction.operands().size();
			     ++index) {
				MachineOperand &operand = instruction.operands()[index];
				if (!operand.isVirtualRegister())
					continue;
				const VReg reg = operand.virtualRegister();
				const MachineSlot slot =
				    operandSlot(result.slots, instruction, operand);
				blockSegments[reg].include(slot);
				MutableInterval &interval = mutableIntervals[reg];
				interval.operands.push_back(LiveRangeOperand{
				    block, &instruction, index, slot, operand.isDef});
				if (!operand.isDef)
					interval.weight += blockWeight;
				interval.spillCost +=
				    blockWeight * (operand.isDef ? storeCost : loadCost);
			}
		}
		for (VReg reg : result.blockLiveOut[block]) {
			MutableSegment &segment = blockSegments[reg];
			if (segment.begin == std::numeric_limits<MachineSlot>::max())
				segment.include(blockSlots.begin);
			segment.end = std::max(segment.end, blockSlots.end);
		}
		for (const auto &[reg, segment] : blockSegments) {
			if (segment.begin == std::numeric_limits<MachineSlot>::max())
				continue;
			MutableInterval &interval = mutableIntervals[reg];
			interval.segments.push_back(
			    MachineSlotRange{segment.begin, segment.end});
		}
	}

	result.intervals.reserve(mutableIntervals.size());
	for (auto &[reg, interval] : mutableIntervals) {
		mergeSegments(interval.segments);
		if (interval.segments.empty())
			continue;
		std::sort(interval.operands.begin(), interval.operands.end(),
		          [](const LiveRangeOperand &lhs, const LiveRangeOperand &rhs) {
			          if (lhs.slot != rhs.slot)
				          return lhs.slot < rhs.slot;
			          return lhs.operandIndex < rhs.operandIndex;
		          });
		result.intervalIndex.emplace(reg, result.intervals.size());
		result.intervals.push_back(LiveInterval{
		    reg, function.registerInfo().get(reg).regClass,
		    std::max(1.0, interval.weight), std::max(1.0, interval.spillCost),
		    liveAcrossCalls.count(reg) != 0, std::move(interval.segments),
		    std::move(interval.operands)});
	}

	function.setProperty(MachineProperty::TracksLiveness);
	return result;
}

} // namespace backend::aarch64
