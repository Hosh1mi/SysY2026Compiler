#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <set>
#include <string>
#include <vector>

// Delay a pure add/sub to a fallthrough copy-back:
//
//   add temp, src, #imm
//   ...
//   b.cond exit
//   mov dst, temp
//
// becomes:
//
//   ...
//   b.cond exit
//   add dst, src, #imm
//
// The operation now executes on exactly the path where the copy executed.
// Source registers must remain unchanged across the move, and precise
// instruction-CFG liveness must prove that the old temporary dies at the copy.
static bool tryMachineDelayAddSubToCopy(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	const MachineInstr &alu = block.instrs[idx];
	if (alu.isLabelLike) return false;
	if (alu.opcodeText != "add" && alu.opcodeText != "sub") return false;
	if (alu.asmOperands.size() != 3) return false;

	const std::string tempReg = alu.asmOperands[0];
	char cls = peephRegClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (peephRegClass(alu.asmOperands[1]) != cls && alu.asmOperands[1] != "sp")
		return false;
	if (alu.asmOperands[2].empty() || alu.asmOperands[2][0] != '#')
		return false;

	std::set<std::string> sourceRegs = block.instrs[idx].uses;
	bool sawConditionalBranch = false;
	std::string branchTarget;
	int seen = 0;
	for (size_t i = idx + 1; i < block.instrs.size() && seen < 10; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (peephIsInertLine(line))
			continue;
		if (line.isLabelLike)
			return false;
		++seen;

		bool isCopy = line.opcodeText == "mov" && line.asmOperands.size() == 2 &&
		              line.asmOperands[1] == tempReg;
		if (isCopy) {
			if (!sawConditionalBranch) return false;
			const std::string &dstReg = line.asmOperands[0];
			if (peephRegClass(dstReg) != cls || dstReg == tempReg) return false;

			auto liveIt = liveness.instrLiveOut.find(&block.instrs[i]);
			if (liveIt == liveness.instrLiveOut.end()) return false;
			auto targetLiveIt = liveness.labelLiveIn.find(branchTarget);
			if (targetLiveIt == liveness.labelLiveIn.end()) return false;
			for (const auto &def : block.instrs[idx].defs) {
				if (liveIt->second.count(def))
					return false;
				if (targetLiveIt->second.count(def))
					return false;
			}

			std::vector<std::string> operands = alu.asmOperands;
			operands[0] = dstReg;
			peephReplaceInstr(block.instrs[i],
			                    peephMakeInsn(alu.opcodeText, operands));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}

		if (peephLineReadsReg(line, tempReg) || peephLineWritesReg(line, tempReg))
			return false;
		for (const auto &source : sourceRegs) {
			std::string physical = source;
			if (!physical.empty() && physical[0] == 'r')
				physical = std::string(1, cls) + physical.substr(1);
			if (peephLineWritesReg(line, physical))
				return false;
		}

		if (line.isCall || line.opcodeText == "b" ||
		    line.opcodeText == "ret")
			return false;
		if (peephIsControlFlowBarrier(line)) {
			if (sawConditionalBranch)
				return false;
			if (line.opcodeText.size() < 3 ||
			    line.opcodeText[0] != 'b' || line.opcodeText[1] != '.' ||
			    line.asmOperands.size() != 1)
				return false;
			sawConditionalBranch = true;
			branchTarget = line.asmOperands[0];
		}
	}
	return false;
}

bool runMachineCodeMotion(MachineFunction &func,
                          const MachineLivenessResult &liveness) {
	for (auto &block : func.blocks)
		for (size_t i = 0; i < block.instrs.size(); ++i)
			if (tryMachineDelayAddSubToCopy(block, i, liveness))
				return true;
	return false;
}
