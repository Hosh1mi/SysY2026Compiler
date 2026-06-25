#include "../../include/backend/arm64/machinePeephole.hpp"
#include "../../include/backend/arm64/machinePeepholeUtils.hpp"
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <vector>




// ── tryMachineSelfMove: used only by runMachinePeephole ────────────

static bool tryMachineSelfMove(MachineBasicBlock &block, size_t idx) {
	auto &inst = block.instrs[idx];
	const MachineInstr &line = inst;
	if (line.isLabelLike) return false;
	if (line.opcodeText != "mov" && line.opcodeText != "fmov") return false;
	if (line.rawOperands.size() < 2) return false;
	if (line.rawOperands[0] != line.rawOperands[1]) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}



static bool tryMachineDelayAddSubToCopy(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	const MachineInstr &alu = block.instrs[idx];
	if (alu.isLabelLike) return false;
	if (alu.opcodeText != "add" && alu.opcodeText != "sub") return false;
	if (alu.rawOperands.size() != 3) return false;

	const std::string tempReg = alu.rawOperands[0];
	char cls = peephRegClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (peephRegClass(alu.rawOperands[1]) != cls && alu.rawOperands[1] != "sp")
		return false;
	if (alu.rawOperands[2].empty() || alu.rawOperands[2][0] != '#')
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

		bool isCopy = line.opcodeText == "mov" && line.rawOperands.size() == 2 &&
		              line.rawOperands[1] == tempReg;
		if (isCopy) {
			if (!sawConditionalBranch) return false;
			const std::string &dstReg = line.rawOperands[0];
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

			std::vector<std::string> operands = alu.rawOperands;
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
			    line.rawOperands.size() != 1)
				return false;
			sawConditionalBranch = true;
			branchTarget = line.rawOperands[0];
		}
	}
	return false;
}

static bool tryMachineRedundantAdrp(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike) return false;
	if (first.opcodeText != "adrp" || first.rawOperands.size() < 2) return false;

	const std::string &reg = first.rawOperands[0];
	const std::string &symbol = first.rawOperands[1];

	auto window = peephInstrWindow(block, idx + 1, 20);
	for (size_t wi : window) {
		const MachineInstr &line = block.instrs[wi];
		if (line.isLabelLike) continue;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;
		if (peephLineWritesReg(line, reg)) return false;

		if (line.opcodeText == "adrp" && line.rawOperands.size() >= 2 &&
		    line.rawOperands[0] == reg && line.rawOperands[1] == symbol) {
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}
	}

	return false;
}

static bool tryMachineRedundantSubFrame(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike) return false;
	if (first.opcodeText != "sub" || first.rawOperands.size() != 3) return false;
	if (first.rawOperands[0] != "x17" || first.rawOperands[1] != "x29") return false;
	const std::string &imm = first.rawOperands[2];
	if (imm.empty() || imm[0] != '#') return false;

	auto window = peephInstrWindow(block, idx + 1, 3);
	for (size_t wi : window) {
		const MachineInstr &line = block.instrs[wi];
		if (line.isLabelLike) continue;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;

		if (line.opcodeText == "sub" && line.rawOperands.size() == 3 &&
		    line.rawOperands[0] == "x17" && line.rawOperands[1] == "x29") {
			if (line.rawOperands[2] != imm) return false;
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}

		if (peephLineWritesReg(line, "x17")) return false;
	}

	return false;
}

static std::string parseFullVectorListReg(const std::string &operand) {
	std::string text = peephTrim(operand);
	if (text.size() < 7 || text.front() != '{' || text.back() != '}')
		return "";
	text = peephTrim(text.substr(1, text.size() - 2));
	if (text.empty() || text[0] != 'v')
		return "";

	size_t pos = 1;
	while (pos < text.size() && std::isdigit(text[pos]))
		++pos;
	if (pos == 1 || pos >= text.size() || text[pos] != '.')
		return "";

	std::string suffix = text.substr(pos);
	if (suffix != ".16b" && suffix != ".8h" && suffix != ".4s" && suffix != ".2d")
		return "";
	return "q" + text.substr(1, pos - 1);
}

static bool tryMachineVectorLdStAlias(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &line = block.instrs[idx];
	if (line.isLabelLike)
		return false;
	if (line.opcodeText != "ld1" && line.opcodeText != "st1")
		return false;
	if (line.rawOperands.size() != 2 && line.rawOperands.size() != 3)
		return false;

	std::string qReg = parseFullVectorListReg(line.rawOperands[0]);
	if (qReg.empty())
		return false;

	MemOperand addr = peephParseMemOp(line.rawOperands[1]);
	if (!addr.valid || addr.offset != 0)
		return false;
	if (line.rawOperands.size() == 3 && line.rawOperands[2] != "#16")
		return false;

	std::vector<std::string> operands = {qReg, line.rawOperands[1]};
	if (line.rawOperands.size() == 3)
		operands.push_back(line.rawOperands[2]);
	peephReplaceInstr(block.instrs[idx],
	                    peephMakeInsn(line.opcodeText == "ld1" ? "ldr" : "str",
	                                    operands));
	return true;
}

bool runMachineCodeMotion(MachineFunction &func) {
	MachineLivenessResult liveness = MachineLiveness().analyze(func);
	for (auto &block : func.blocks)
		for (size_t i = 0; i < block.instrs.size(); ++i)
			if (tryMachineDelayAddSubToCopy(block, i, liveness))
				return true;
	return false;
}

bool runMachineCanonicalization(MachineFunction &func) {
	for (auto &block : func.blocks)
		for (size_t i = 0; i < block.instrs.size(); ++i)
			if (tryMachineVectorLdStAlias(block, i))
				return true;
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

bool runMachinePeephole(MachineFunction &func) {
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineSelfMove(block, i))
				return true;
		}
	}
	return false;
}
