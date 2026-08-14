// This file performs final virtual-register spilling after splitting and
// recoloring have failed, including target-aware rematerialization.
#include "backend/regalloc.hpp"

#include "backend/machine_analysis.hpp"
#include "backend/rematerialization.hpp"

#include <unordered_set>

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

} // namespace

void GraphColoringRegisterAllocator::insertSpills(
    MachineFunction &function, const std::vector<VReg> &spills,
    std::unordered_map<VReg, int> &spillSlots) const {
	std::unordered_set<VReg> spilled(spills.begin(), spills.end());
	MachineRegisterIndex registers(function);
	RematerializationAnalysis rematerializationAnalysis;
	RematerializationAnalysis::RecipeMap rematerializations =
	    rematerializationAnalysis.analyze(function, registers, spills);

	for (VReg reg : spills) {
		if (rematerializations.count(reg) || spillSlots.count(reg))
			continue;
		RegClass regClass = function.registerInfo().get(reg).regClass;
		spillSlots.emplace(reg,
		                   function.frameInfo().createStackObject(
		                       RegisterInfo::spillSize(regClass),
		                       RegisterInfo::spillAlignment(regClass), true));
	}

	for (const auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end(); ++it) {
			std::unordered_map<VReg, VReg> temporaries;
			std::unordered_set<VReg> needsLoad;
			std::unordered_set<VReg> needsStore;
			for (const MachineOperand &operand : it->operands()) {
				if (!operand.isVirtualRegister() ||
				    !spilled.count(operand.virtualRegister()))
					continue;
				VReg old = operand.virtualRegister();
				(operand.isDef ? needsStore : needsLoad).insert(old);
				if (temporaries.count(old))
					continue;
				const VRegInfo &info = function.registerInfo().get(old);
				VReg temporary = function.registerInfo().createVirtualRegister(
				    info.regClass, info.valueType);
				function.registerInfo().get(temporary).spillTemporary = true;
				temporaries.emplace(old, temporary);
			}
			if (temporaries.empty())
				continue;

			for (VReg old : needsLoad) {
				VReg temporary = temporaries.at(old);
				RegClass regClass = function.registerInfo().get(old).regClass;
				auto rematerialization = rematerializations.find(old);
				if (rematerialization != rematerializations.end()) {
					MachineInstr materialized =
					    rematerialization->second.clone(temporary);
					auto inserted =
					    instructions.insert(it, std::move(materialized));
					function.registerInfo().setDefinition(temporary,
					                                      &*inserted);
					continue;
				}
				MachineInstr load(Opcode::SPILL_LOAD);
				load.addOperand(MachineOperand::vreg(temporary, regClass, true))
				    .addOperand(MachineOperand::frameIndex(spillSlots.at(old)));
				load.addMemoryOperand(
				    MachineMemOperand{MachineMemOperand::Access::Load,
				                      RegisterInfo::spillSize(regClass),
				                      RegisterInfo::spillAlignment(regClass),
				                      nullptr, spillSlots.at(old), 0, false});
				auto inserted = instructions.insert(it, std::move(load));
				function.registerInfo().setDefinition(temporary, &*inserted);
			}

			for (MachineOperand &operand : it->operands()) {
				if (!operand.isVirtualRegister() ||
				    !spilled.count(operand.virtualRegister()))
					continue;
				VReg old = operand.virtualRegister();
				operand = replacementRegister(
				    operand, temporaries.at(old),
				    function.registerInfo().get(old).regClass);
				if (operand.isDef)
					function.registerInfo().setDefinition(temporaries.at(old),
					                                      &*it);
			}

			auto after = std::next(it);
			for (VReg old : needsStore) {
				if (rematerializations.count(old))
					continue;
				VReg temporary = temporaries.at(old);
				RegClass regClass = function.registerInfo().get(old).regClass;
				MachineInstr store(Opcode::SPILL_STORE);
				store.addOperand(MachineOperand::vreg(temporary, regClass))
				    .addOperand(MachineOperand::frameIndex(spillSlots.at(old)));
				store.addMemoryOperand(
				    MachineMemOperand{MachineMemOperand::Access::Store,
				                      RegisterInfo::spillSize(regClass),
				                      RegisterInfo::spillAlignment(regClass),
				                      nullptr, spillSlots.at(old), 0, false});
				instructions.insert(after, std::move(store));
			}
		}
	}

	// A rematerialized parent no longer has uses.  Delete its original
	// definition so the next allocation round does not reserve a color for it.
	std::unordered_set<MachineInstr *> deadDefinitions;
	for (const auto &[reg, recipe] : rematerializations) {
		(void)reg;
		deadDefinitions.insert(recipe.definition);
	}
	for (const auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end();) {
			if (!deadDefinitions.count(&*it)) {
				++it;
				continue;
			}
			it = instructions.erase(it);
		}
	}
	function.clearProperty(MachineProperty::TracksLiveness);
}

} // namespace backend::aarch64
