#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <string>

static bool tryMachineRedundantAdrp(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike) return false;
	if (first.opcodeText != "adrp" || first.asmOperands.size() < 2) return false;

	const std::string &reg = first.asmOperands[0];
	const std::string &symbol = first.asmOperands[1];

	auto window = peephInstrWindow(block, idx + 1, 20);
	for (size_t wi : window) {
		const MachineInstr &line = block.instrs[wi];
		if (line.isLabelLike) continue;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;
		if (peephLineWritesReg(line, reg)) return false;

		if (line.opcodeText == "adrp" && line.asmOperands.size() >= 2 &&
		    line.asmOperands[0] == reg && line.asmOperands[1] == symbol) {
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}
	}

	return false;
}

static bool tryMachineRedundantSubFrame(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike) return false;
	if (first.opcodeText != "sub" || first.asmOperands.size() != 3) return false;
	if (first.asmOperands[0] != "x17" || first.asmOperands[1] != "x29") return false;
	const std::string &imm = first.asmOperands[2];
	if (imm.empty() || imm[0] != '#') return false;

	auto window = peephInstrWindow(block, idx + 1, 3);
	for (size_t wi : window) {
		const MachineInstr &line = block.instrs[wi];
		if (line.isLabelLike) continue;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;

		if (line.opcodeText == "sub" && line.asmOperands.size() == 3 &&
		    line.asmOperands[0] == "x17" && line.asmOperands[1] == "x29") {
			if (line.asmOperands[2] != imm) return false;
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}

		if (peephLineWritesReg(line, "x17")) return false;
	}

	return false;
}

bool runMachineLocalCSE(MachineFunction &func) {
	for (auto &block : func.blocks)
		for (size_t i = 0; i < block.instrs.size(); ++i)
			if (tryMachineRedundantAdrp(block, i) ||
			    tryMachineRedundantSubFrame(block, i))
				return true;
	return false;
}
