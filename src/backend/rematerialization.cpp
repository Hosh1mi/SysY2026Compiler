// This file validates and clones rematerializable Machine definitions without
// coupling the spiller to individual target opcodes.
#include "../include/backend/rematerialization.hpp"

namespace backend::aarch64 {

MachineInstr RematerializationRecipe::clone(VReg destination) const {
	MachineInstr materialized = *definition;
	const MachineOperand &oldDef = materialized.operands().front();
	MachineOperand replacement =
	    MachineOperand::vreg(destination, regClass, true);
	replacement.isImplicit = oldDef.isImplicit;
	replacement.isDead = oldDef.isDead;
	replacement.isUndef = oldDef.isUndef;
	replacement.isEarlyClobber = oldDef.isEarlyClobber;
	replacement.isRenamable = oldDef.isRenamable;
	replacement.tiedTo = oldDef.tiedTo;
	materialized.operands().front() = replacement;
	return materialized;
}

RematerializationAnalysis::RecipeMap
RematerializationAnalysis::analyze(MachineFunction &function,
                                   const MachineRegisterIndex &registers,
                                   const std::vector<VReg> &candidates) const {
	RecipeMap recipes;
	for (VReg reg : candidates) {
		MachineInstr *definition = registers.uniqueDefinition(reg);
		if (!definition || definition->operands().empty())
			continue;
		const MachineOperand &destination = definition->operands().front();
		if (!destination.isVirtualRegister() || !destination.isDef ||
		    destination.virtualRegister() != reg)
			continue;

		const unsigned cost =
		    InstrInfo::rematerializationCost(definition->opcode());
		if (!cost)
			continue;
		const InstrDesc descriptor = InstrInfo::get(definition->opcode());
		if (descriptor.mayLoad || descriptor.mayStore ||
		    descriptor.hasSideEffects || descriptor.call ||
		    descriptor.terminator)
			continue;
		const unsigned reloadCost = InstrInfo::get(Opcode::SPILL_LOAD).latency;
		if (cost >= reloadCost)
			continue;

		bool dependsOnVirtualRegister = false;
		for (const MachineOperand &operand : definition->operands())
			dependsOnVirtualRegister |=
			    operand.isVirtualRegister() && !operand.isDef;
		if (dependsOnVirtualRegister)
			continue;

		recipes.emplace(reg, RematerializationRecipe{
		                         function.registerInfo().get(reg).regClass,
		                         definition, cost});
	}
	return recipes;
}

} // namespace backend::aarch64
