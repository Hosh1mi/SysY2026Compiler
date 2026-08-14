// This file implements checked Machine CFG edge splitting while keeping
// successor/predecessor lists and branch operands synchronized.
#include "backend/machine_cfg_edit.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace backend::aarch64 {
namespace {

bool hasSuccessor(const MachineBasicBlock &predecessor,
                  const MachineBasicBlock &successor) {
	return std::find(predecessor.successors().begin(),
	                 predecessor.successors().end(),
	                 &successor) != predecessor.successors().end();
}

bool targetsSuccessor(const MachineInstr &instruction,
                      const MachineBasicBlock &successor) {
	if (!instruction.isBranch())
		return false;
	for (const MachineOperand &operand : instruction.operands())
		if (operand.kind() == MachineOperand::Kind::BasicBlock &&
		    operand.basicBlock() == &successor)
			return true;
	return false;
}

} // namespace

bool MachineCFGEdit::canSplitEdge(const MachineBasicBlock &predecessor,
                                  const MachineBasicBlock &successor) {
	if (!hasSuccessor(predecessor, successor))
		return false;
	for (const MachineInstr &instruction : predecessor.instructions())
		if (targetsSuccessor(instruction, successor))
			return true;
	return false;
}

MachineBasicBlock &MachineCFGEdit::splitEdge(MachineFunction &function,
                                             MachineBasicBlock &predecessor,
                                             MachineBasicBlock &successor,
                                             std::string name) {
	if (!canSplitEdge(predecessor, successor))
		throw std::logic_error(
		    "cannot split an implicit or missing Machine edge");

	MachineBasicBlock &split = function.createBlock(std::move(name));
	split.frequency = std::min(predecessor.frequency, successor.frequency);
	split.loopDepth = std::min(predecessor.loopDepth, successor.loopDepth);

	predecessor.removeSuccessor(&successor);
	predecessor.addSuccessor(&split);
	split.addSuccessor(&successor);

	for (MachineInstr &instruction : predecessor.instructions()) {
		if (!instruction.isBranch())
			continue;
		for (MachineOperand &operand : instruction.operands())
			if (operand.kind() == MachineOperand::Kind::BasicBlock &&
			    operand.basicBlock() == &successor)
				operand = MachineOperand::block(&split);
	}

	MachineInstr branch(Opcode::B);
	branch.addOperand(MachineOperand::block(&successor));
	split.append(std::move(branch));
	function.clearProperty(MachineProperty::TracksLiveness);
	return split;
}

} // namespace backend::aarch64
