// This file forwards values already resident in physical registers across
// allocator spill slots and removes stores proven dead by CFG dataflow.
#include "../include/backend/spill_optimization.hpp"

#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace backend::aarch64 {
namespace {

using SlotSet = std::set<int>;

std::optional<int> allocatorSpillSlot(const MachineFunction &function,
                                      const MachineInstr &instruction) {
	if (instruction.opcode() != Opcode::SPILL_LOAD &&
	    instruction.opcode() != Opcode::SPILL_STORE)
		return std::nullopt;
	if (instruction.operands().size() < 2 ||
	    instruction.operands()[1].kind() != MachineOperand::Kind::FrameIndex)
		return std::nullopt;
	const int slot = instruction.operands()[1].frameIndex();
	if (!function.frameInfo().getObject(slot).spill)
		return std::nullopt;
	return slot;
}

void invalidateRegister(std::unordered_map<int, PhysReg> &knownValues,
                        PhysReg reg) {
	for (auto value = knownValues.begin(); value != knownValues.end();) {
		if (RegisterInfo::aliases(value->second, reg))
			value = knownValues.erase(value);
		else
			++value;
	}
}

bool forwardKnownValues(MachineFunction &function) {
	bool changed = false;
	for (const auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		std::unordered_map<int, PhysReg> knownValues;
		for (auto instruction = instructions.begin();
		     instruction != instructions.end();) {
			const std::optional<int> slot =
			    allocatorSpillSlot(function, *instruction);
			const bool load = instruction->opcode() == Opcode::SPILL_LOAD;
			const bool store = instruction->opcode() == Opcode::SPILL_STORE;

			if (slot && load && !instruction->operands().empty() &&
			    instruction->operands()[0].isPhysicalRegister()) {
				const PhysReg destination =
				    instruction->operands()[0].physicalRegister();
				auto known = knownValues.find(*slot);
				if (known != knownValues.end() &&
				    RegisterInfo::aliases(known->second, destination)) {
					instruction = instructions.erase(instruction);
					changed = true;
					continue;
				}

				PhysReg source = PhysReg::NoReg;
				if (known != knownValues.end())
					source = known->second;
				invalidateRegister(knownValues, destination);
				if (source != PhysReg::NoReg) {
					const RegClass regClass =
					    instruction->operands()[0].regClass();
					MachineInstr copy(Opcode::COPY);
					copy.addOperand(MachineOperand::physReg(destination,
					                                        regClass, true))
					    .addOperand(MachineOperand::physReg(source, regClass));
					*instruction = std::move(copy);
					changed = true;
				}
				knownValues[*slot] = destination;
				++instruction;
				continue;
			}

			if (slot && store && !instruction->operands().empty() &&
			    instruction->operands()[0].isPhysicalRegister()) {
				knownValues[*slot] =
				    instruction->operands()[0].physicalRegister();
				++instruction;
				continue;
			}

			if (instruction->isCall())
				knownValues.clear();
			else
				for (const MachineOperand &operand : instruction->operands())
					if (operand.isPhysicalRegister() && operand.isDef)
						invalidateRegister(knownValues,
						                   operand.physicalRegister());
			++instruction;
		}
	}
	return changed;
}

bool removeDeadStores(MachineFunction &function) {
	std::unordered_map<MachineBasicBlock *, SlotSet> uses;
	std::unordered_map<MachineBasicBlock *, SlotSet> defs;
	std::unordered_map<MachineBasicBlock *, SlotSet> liveIn;
	std::unordered_map<MachineBasicBlock *, SlotSet> liveOut;

	for (const auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		for (const MachineInstr &instruction : block->instructions()) {
			const std::optional<int> slot =
			    allocatorSpillSlot(function, instruction);
			if (!slot)
				continue;
			if (instruction.opcode() == Opcode::SPILL_STORE)
				defs[block].insert(*slot);
			else if (!defs[block].count(*slot))
				uses[block].insert(*slot);
		}
	}

	bool dataflowChanged = true;
	while (dataflowChanged) {
		dataflowChanged = false;
		for (auto block = function.blocks().rbegin();
		     block != function.blocks().rend(); ++block) {
			MachineBasicBlock *current = block->get();
			SlotSet nextOut;
			for (MachineBasicBlock *successor : current->successors())
				nextOut.insert(liveIn[successor].begin(),
				               liveIn[successor].end());
			SlotSet nextIn = uses[current];
			for (int slot : nextOut)
				if (!defs[current].count(slot))
					nextIn.insert(slot);
			if (nextOut != liveOut[current] || nextIn != liveIn[current]) {
				liveOut[current] = std::move(nextOut);
				liveIn[current] = std::move(nextIn);
				dataflowChanged = true;
			}
		}
	}

	bool changed = false;
	for (const auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		auto &instructions = block->instructions();
		using Iterator = MachineBasicBlock::InstrList::iterator;
		std::unordered_map<int, Iterator> unreadStores;
		for (auto instruction = instructions.begin();
		     instruction != instructions.end(); ++instruction) {
			const std::optional<int> slot =
			    allocatorSpillSlot(function, *instruction);
			if (!slot)
				continue;
			if (instruction->opcode() == Opcode::SPILL_LOAD) {
				unreadStores.erase(*slot);
				continue;
			}
			auto previous = unreadStores.find(*slot);
			if (previous != unreadStores.end()) {
				instructions.erase(previous->second);
				changed = true;
			}
			unreadStores[*slot] = instruction;
		}
		for (const auto &[slot, store] : unreadStores)
			if (!liveOut[block].count(slot)) {
				instructions.erase(store);
				changed = true;
			}
	}
	return changed;
}

} // namespace

bool PostRASpillSlotOptimizer::run(MachineFunction &function) {
	const bool forwarded = forwardKnownValues(function);
	const bool storesRemoved = removeDeadStores(function);
	return forwarded || storesRemoved;
}

} // namespace backend::aarch64
