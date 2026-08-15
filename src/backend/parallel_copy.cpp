// This file lowers SSA PHIs to parallel virtual copies and resolves their
// remaining physical-register groups after register allocation.
#include "backend/regalloc.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

namespace backend::aarch64 {

bool PhiElimination::run(MachineFunction &function) {
	if (!function.hasProperty(MachineProperty::HasPHIs))
		return false;
	struct Edge {
		MachineBasicBlock *predecessor;
		MachineBasicBlock *successor;
	};
	std::vector<Edge> edgesToSplit;
	for (const auto &successor : function.blocks()) {
		bool hasPhi = !successor->instructions().empty() &&
		              successor->instructions().front().opcode() == Opcode::PHI;
		if (!hasPhi)
			continue;
		for (MachineBasicBlock *predecessor : successor->predecessors())
			if (predecessor->successors().size() > 1)
				edgesToSplit.push_back({predecessor, successor.get()});
	}

	unsigned splitNumber = 0;
	for (const Edge &edge : edgesToSplit) {
		if (std::find(edge.predecessor->successors().begin(),
		              edge.predecessor->successors().end(), edge.successor) ==
		    edge.predecessor->successors().end())
			std::abort();
		bool explicitEdge = false;
		for (const MachineInstr &instruction :
		     edge.predecessor->instructions()) {
			if (!instruction.isBranch())
				continue;
			for (const MachineOperand &operand : instruction.operands())
				explicitEdge |=
				    operand.kind() == MachineOperand::Kind::BasicBlock &&
				    operand.basicBlock() == edge.successor;
		}
		if (!explicitEdge)
			std::abort();

		MachineBasicBlock &split = function.createBlock(
		    "phi.edge." + std::to_string(splitNumber++));
		split.frequency =
		    std::min(edge.predecessor->frequency, edge.successor->frequency);
		split.loopDepth =
		    std::min(edge.predecessor->loopDepth, edge.successor->loopDepth);
		edge.predecessor->removeSuccessor(edge.successor);
		edge.predecessor->addSuccessor(&split);
		split.addSuccessor(edge.successor);
		for (MachineInstr &instruction : edge.predecessor->instructions()) {
			if (!instruction.isBranch())
				continue;
			for (MachineOperand &operand : instruction.operands())
				if (operand.kind() == MachineOperand::Kind::BasicBlock &&
				    operand.basicBlock() == edge.successor)
					operand = MachineOperand::block(&split);
		}
		MachineInstr branch(Opcode::B);
		branch.addOperand(MachineOperand::block(edge.successor));
		split.append(std::move(branch));
		function.clearProperty(MachineProperty::TracksLiveness);

		for (MachineInstr &instruction : edge.successor->instructions()) {
			if (instruction.opcode() != Opcode::PHI)
				break;
			for (std::size_t i = 2; i < instruction.operands().size(); i += 2)
				if (instruction.operands()[i].kind() ==
				        MachineOperand::Kind::BasicBlock &&
				    instruction.operands()[i].basicBlock() == edge.predecessor)
					instruction.operands()[i] = MachineOperand::block(&split);
		}
	}

	struct Copy {
		VReg destination;
		VReg source;
		RegClass regClass;
	};
	std::unordered_map<MachineBasicBlock *, std::vector<Copy>> copies;
	bool changed = false;
	for (const auto &owned : function.blocks()) {
		MachineBasicBlock &successor = *owned;
		auto it = successor.instructions().begin();
		while (it != successor.instructions().end() &&
		       it->opcode() == Opcode::PHI) {
			if (it->operands().empty() ||
			    !it->operands()[0].isVirtualRegister() ||
			    !it->operands()[0].isDef)
				std::abort();
			VReg destination = it->operands()[0].virtualRegister();
			RegClass regClass = it->operands()[0].regClass();
			function.registerInfo().setDefinition(destination, nullptr);
			for (std::size_t i = 1; i + 1 < it->operands().size(); i += 2) {
				if (!it->operands()[i].isVirtualRegister() ||
				    it->operands()[i + 1].kind() !=
				        MachineOperand::Kind::BasicBlock)
					std::abort();
				copies[it->operands()[i + 1].basicBlock()].push_back(
				    Copy{destination, it->operands()[i].virtualRegister(),
				         regClass});
			}
			it = successor.instructions().erase(it);
			changed = true;
		}
	}

	for (auto &[block, pending] : copies) {
		auto insertion = block->instructions().begin();
		while (insertion != block->instructions().end() &&
		       !insertion->isTerminator())
			++insertion;
		while (!pending.empty()) {
			std::unordered_set<VReg> sources;
			for (const Copy &copy : pending)
				sources.insert(copy.source);
			auto ready = pending.end();
			for (auto candidate = pending.begin(); candidate != pending.end();
			     ++candidate)
				if (candidate->destination == candidate->source ||
				    !sources.count(candidate->destination)) {
					ready = candidate;
					break;
				}
			if (ready != pending.end()) {
				Copy copy = *ready;
				pending.erase(ready);
				if (copy.destination == copy.source)
					continue;
				MachineInstr instruction(Opcode::COPY);
				instruction
				    .addOperand(MachineOperand::vreg(copy.destination,
				                                     copy.regClass, true))
				    .addOperand(
				        MachineOperand::vreg(copy.source, copy.regClass));
				block->instructions().insert(insertion, std::move(instruction));
				continue;
			}

			Copy cycle = pending.front();
			VReg temporary = function.registerInfo().createVirtualRegister(
			    cycle.regClass,
			    function.registerInfo().get(cycle.source).valueType);
			MachineInstr save(Opcode::COPY);
			save.addOperand(
			        MachineOperand::vreg(temporary, cycle.regClass, true))
			    .addOperand(MachineOperand::vreg(cycle.source, cycle.regClass));
			block->instructions().insert(insertion, std::move(save));
			for (Copy &copy : pending)
				if (copy.source == cycle.source)
					copy.source = temporary;
		}
	}

	if (changed) {
		function.clearProperty(MachineProperty::IsSSA);
		function.clearProperty(MachineProperty::TracksLiveness);
	}
	function.clearProperty(MachineProperty::HasPHIs);
	return changed;
}

bool PostRAParallelCopyResolver::run(MachineFunction &function) {

	struct Copy {
		PhysReg destination = PhysReg::NoReg;
		PhysReg source = PhysReg::NoReg;
		RegClass regClass = RegClass::Invalid;
	};
	bool changed = false;
	for (const auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end();) {
			if (!it->parallelCopyGroup) {
				++it;
				continue;
			}

			const unsigned group = it->parallelCopyGroup;
			std::vector<Copy> pending;
			while (it != instructions.end() && it->parallelCopyGroup == group) {
				if (it->opcode() != Opcode::COPY ||
				    it->operands().size() != 2 ||
				    !it->operands()[0].isPhysicalRegister() ||
				    !it->operands()[1].isPhysicalRegister())
					std::abort();
				pending.push_back(Copy{it->operands()[0].physicalRegister(),
				                       it->operands()[1].physicalRegister(),
				                       it->operands()[0].regClass()});
				it = instructions.erase(it);
			}
			auto insertion = it;

			while (!pending.empty()) {
				std::unordered_set<PhysReg> sources;
				for (const Copy &copy : pending)
					sources.insert(copy.source);
				auto ready = pending.end();
				for (auto candidate = pending.begin(); candidate != pending.end();
				     ++candidate)
					if (candidate->destination == candidate->source ||
					    !sources.count(candidate->destination)) {
						ready = candidate;
						break;
					}
				if (ready != pending.end()) {
					Copy copy = *ready;
					pending.erase(ready);
					if (copy.destination != copy.source) {
						MachineInstr instruction(Opcode::COPY);
						instruction
						    .addOperand(MachineOperand::physReg(
						        copy.destination, copy.regClass, true))
						    .addOperand(MachineOperand::physReg(copy.source,
						                                        copy.regClass));
						instructions.insert(insertion, std::move(instruction));
					}
					continue;
				}

				std::abort();
			}
			changed = true;
		}
	}
	return changed;
}

} // namespace backend::aarch64
