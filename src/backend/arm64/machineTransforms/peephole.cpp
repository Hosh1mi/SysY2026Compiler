#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"

static bool tryMachineSelfMove(MachineBasicBlock &block, size_t idx) {
	auto &inst = block.instrs[idx];
	const MachineInstr &line = inst;
	if (line.isLabelLike) return false;
	if (line.opcodeText != "mov" && line.opcodeText != "fmov") return false;
	if (line.asmOperands.size() < 2) return false;
	if (line.asmOperands[0] != line.asmOperands[1]) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

bool runMachinePeephole(MachineFunction &func) {
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineSelfMove(block, i))
				return true;
		}
	}
	return false;
}
