// This file applies validated local split plans by placing a spill/reload or
// rematerialization at the gap boundaries and rewriting the resumed sibling.
#include "backend/live_range_edit.hpp"

#include <algorithm>
#include <cstdlib>

namespace backend::aarch64 {
namespace {

MachineOperand replacementRegister(const MachineOperand &old, VReg reg,
                                   RegClass regClass) {
	MachineOperand replacement = MachineOperand::vreg(reg, regClass, old.isDef);
	replacement.isImplicit = old.isImplicit;
	replacement.isKill = old.isKill;
	replacement.isDead = old.isDead;
	replacement.isUndef = old.isUndef;
	replacement.isEarlyClobber = old.isEarlyClobber;
	replacement.isRenamable = old.isRenamable;
	replacement.tiedTo = old.tiedTo;
	return replacement;
}

MachineBasicBlock::InstrList::iterator
findInstruction(MachineBasicBlock &block, MachineInstr *instruction) {
	MachineBasicBlock::InstrList::iterator it = block.instructions().begin();
	for (; it != block.instructions().end(); ++it)
		if (&*it == instruction)
			break;
	return it;
}

int getOrCreateSpillSlot(MachineFunction &function, VReg reg, RegClass regClass,
                         std::unordered_map<VReg, int> &spillSlots) {
	auto existing = spillSlots.find(reg);
	if (existing != spillSlots.end())
		return existing->second;
	const int slot = function.frameInfo().createStackObject(
	    RegisterInfo::spillSize(regClass),
	    RegisterInfo::spillAlignment(regClass), true);
	spillSlots.emplace(reg, slot);
	return slot;
}

MachineInstr makeSpillStore(VReg reg, RegClass regClass, int slot, bool kill) {
	MachineInstr store(Opcode::SPILL_STORE);
	MachineOperand stored = MachineOperand::vreg(reg, regClass);
	stored.isKill = kill;
	store.addOperand(std::move(stored))
	    .addOperand(MachineOperand::frameIndex(slot));
	store.addMemoryOperand(MachineMemOperand{
	    MachineMemOperand::Access::Store, RegisterInfo::spillSize(regClass),
	    RegisterInfo::spillAlignment(regClass), nullptr, slot, 0, false});
	return store;
}

MachineInstr makeSpillLoad(VReg reg, RegClass regClass, int slot) {
	MachineInstr load(Opcode::SPILL_LOAD);
	load.addOperand(MachineOperand::vreg(reg, regClass, true))
	    .addOperand(MachineOperand::frameIndex(slot));
	load.addMemoryOperand(MachineMemOperand{
	    MachineMemOperand::Access::Load, RegisterInfo::spillSize(regClass),
	    RegisterInfo::spillAlignment(regClass), nullptr, slot, 0, false});
	return load;
}

} // namespace

bool LiveRangeEdit::splitLocalGap(
    MachineFunction &function, const LivenessResult &liveness,
    const LocalSplitPlan &plan,
    std::unordered_map<VReg, int> &spillSlots) const {
	const LiveInterval *interval = liveness.find(plan.parent);
	if (!interval || !plan.block || !plan.splitAfter || !plan.resumeBefore ||
	    plan.estimatedBenefit <= plan.estimatedCost)
		return false;

	auto split = findInstruction(*plan.block, plan.splitAfter);
	auto resume = findInstruction(*plan.block, plan.resumeBefore);
	if (split == plan.block->instructions().end() ||
	    resume == plan.block->instructions().end() || split == resume)
		return false;

	const VRegInfo parentInfo = function.registerInfo().get(plan.parent);
	VReg sibling = function.registerInfo().createVirtualRegister(
	    parentInfo.regClass, parentInfo.valueType);
	const unsigned nextGeneration = parentInfo.splitGeneration + 1;
	// Both halves advance to the next stage.  Leaving the original half at
	// generation zero would let its newly inserted spill-store operand form a
	// fresh artificial gap and repeat the same split indefinitely.
	function.registerInfo().get(plan.parent).splitGeneration = nextGeneration;
	function.registerInfo().get(sibling).splitGeneration = nextGeneration;

	bool rewroteUse = false;
	for (const LiveRangeOperand &reference : interval->operands) {
		if (reference.block != plan.block || reference.slot < plan.resumeSlot)
			continue;
		MachineOperand &operand = reference.operand();
		if (operand.isDef)
			std::abort();
		operand = replacementRegister(operand, sibling, parentInfo.regClass);
		rewroteUse = true;
	}
	if (!rewroteUse)
		std::abort();

	if (plan.rematerialization.definition) {
		MachineInstr materialized = plan.rematerialization.clone(sibling);
		auto inserted =
		    plan.block->instructions().insert(resume, std::move(materialized));
		function.registerInfo().setDefinition(sibling, &*inserted);
	} else {
		const int slot = getOrCreateSpillSlot(function, plan.parent,
		                                      parentInfo.regClass, spillSlots);
		plan.block->instructions().insert(
		    std::next(split),
		    makeSpillStore(plan.parent, parentInfo.regClass, slot, true));
		auto inserted = plan.block->instructions().insert(
		    resume, makeSpillLoad(sibling, parentInfo.regClass, slot));
		function.registerInfo().setDefinition(sibling, &*inserted);
	}

	function.clearProperty(MachineProperty::TracksLiveness);
	return true;
}

} // namespace backend::aarch64
