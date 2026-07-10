#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cctype>
#include <set>
#include <string>
#include <vector>

static bool tryMachineSwapMov(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &first = block.instrs[idx];
	const MachineInstr &second = block.instrs[idx + 1];
	if (first.isLabelLike || second.isLabelLike)
		return false;
	if (first.opcodeText != "mov" || second.opcodeText != "mov") return false;
	if (first.rawOperands.size() < 2 || second.rawOperands.size() < 2) return false;

	const std::string &rA = first.rawOperands[0];
	const std::string &rB = first.rawOperands[1];
	if (rA == rB) return false;
	if (rB.empty() || rB[0] == '#') return false;
	if (!peephRegClass(rA) || !peephRegClass(rB)) return false;
	if (second.rawOperands[0] != rB || second.rawOperands[1] != rA) return false;

	block.instrs.erase(block.instrs.begin() + idx + 1);
	return true;
}

static bool tryMachineForwardMov(MachineBasicBlock &block, size_t idx,
                                 const MachineLivenessResult &liveness) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike) return false;
	if (first.opcodeText != "mov" || first.rawOperands.size() != 2) return false;

	const std::string &tempReg = first.rawOperands[0];
	const std::string &srcReg = first.rawOperands[1];
	if (tempReg == srcReg) return false;
	if (!peephRegClass(tempReg)) return false;

	const MachineInstr &second = block.instrs[idx + 1];
	if (second.isLabelLike) return false;
	if (second.opcodeText != "mov" || second.rawOperands.size() != 2) return false;
	if (second.rawOperands[1] != tempReg) return false;

	const std::string &dstReg = second.rawOperands[0];
	if (peephRegClass(dstReg) != peephRegClass(tempReg)) return false;

	if (!peephRegDeadAfter(block, idx + 1, tempReg, liveness)) return false;

	peephReplaceInstr(block.instrs[idx + 1],
	                  peephMakeInsn("mov", {dstReg, srcReg}));
	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool isRetargetablePureDef(const MachineInstr &line) {
	if (line.isLabelLike || line.rawOperands.empty())
		return false;
	if (!peephRegClass(line.rawOperands[0]))
		return false;
	if (line.setsFlags || line.isCall ||
	    peephIsControlFlowBarrier(line))
		return false;

	static const std::set<std::string> mnemonics = {
		"add", "sub", "and", "orr", "eor", "bic",
		"asr", "lsl", "lsr", "neg", "clz",
		"mul", "madd", "msub", "mneg",
		"sdiv", "udiv",
		"fadd", "fsub", "fmul", "fdiv", "fneg",
		"scvtf", "fcvtzs"
	};
	return mnemonics.count(line.opcodeText) != 0;
}

static bool tryMachineRetargetCopyDest(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &producer = block.instrs[idx];
	const MachineInstr &copy = block.instrs[idx + 1];
	if (!isRetargetablePureDef(producer)) return false;
	if (copy.isLabelLike) return false;
	if (copy.opcodeText != "mov" && copy.opcodeText != "fmov") return false;
	if (copy.rawOperands.size() != 2) return false;

	const std::string &tempReg = producer.rawOperands[0];
	const std::string &dstReg = copy.rawOperands[0];
	const std::string &copySrc = copy.rawOperands[1];
	if (!peephSamePhysicalReg(copySrc, tempReg)) return false;
	if (peephSamePhysicalReg(dstReg, tempReg)) {
		block.instrs.erase(block.instrs.begin() + idx + 1);
		return true;
	}

	if (dstReg == "sp" || dstReg == "wzr" || dstReg == "xzr")
		return false;
	char tempCls = peephRegClass(tempReg);
	char dstCls = peephRegClass(dstReg);
	if (!tempCls || !dstCls || tempCls != dstCls)
		return false;
	if (tempCls != 'w' && tempCls != 'x' && tempCls != 's' && tempCls != 'd')
		return false;
	if (copy.opcodeText == "mov" && (tempCls == 's' || tempCls == 'd'))
		return false;
	if (copy.opcodeText == "fmov" && tempCls != 's' && tempCls != 'd')
		return false;

	auto liveIt = liveness.instrLiveOut.find(&block.instrs[idx + 1]);
	if (liveIt == liveness.instrLiveOut.end())
		return false;
	for (const auto &def : block.instrs[idx].defs) {
		if (liveIt->second.count(def))
			return false;
	}

	std::vector<std::string> operands = producer.rawOperands;
	operands[0] = dstReg;
	peephReplaceInstr(block.instrs[idx],
	                  peephMakeInsn(producer.opcodeText, operands));
	block.instrs.erase(block.instrs.begin() + idx + 1);
	return true;
}

// Split a NEON register operand into its number and arrangement, e.g.
// "v10.4s" → num="10", arr=".4s".  Returns false for anything that is not a
// v-register with a lane arrangement.
static bool splitVectorOperand(const std::string &op, std::string &num,
                               std::string &arr) {
	if (op.size() < 4 || op[0] != 'v' || !std::isdigit((unsigned char)op[1]))
		return false;
	size_t dot = op.find('.');
	if (dot == std::string::npos || dot == 1)
		return false;
	num = op.substr(1, dot - 1);
	for (char c : num)
		if (!std::isdigit((unsigned char)c)) return false;
	arr = op.substr(dot);
	return true;
}

// NEON-arrangement-aware counterpart of tryMachineRetargetCopyDest.
//
//   <vec-op> vX.<arr>, ...        ; pure vector def (destination is write-only)
//   mov      vY.16b, vX.16b       ; full-width copy of the result
//     →  <vec-op> vY.<arr>, ...   (copy removed)
//
// The scalar retarget pass refuses the 'v' register class on purpose: it would
// splice the copy's destination spelling (always `.16b`) onto the producer,
// corrupting a lane arrangement such as `.4s`.  Here we keep the producer's
// arrangement and swap only the register number.  Accumulating forms
// (mla / mls / fmla / fmls) and lane inserts read their destination, so they
// are excluded — renaming the destination there would change which prior value
// is folded in.
static bool tryMachineRetargetVectorCopyDest(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &producer = block.instrs[idx];
	const MachineInstr &copy = block.instrs[idx + 1];
	if (producer.isLabelLike || producer.rawOperands.empty())
		return false;
	if (copy.isLabelLike || copy.opcodeText != "mov" ||
	    copy.rawOperands.size() != 2)
		return false;

	static const std::set<std::string> vecPureDef = {
		"movi", "dup", "add", "sub", "mul",
		"fadd", "fsub", "fmul",
		"and", "orr", "eor", "bic",
		"sshl", "sshr", "ushr"};
	if (!vecPureDef.count(producer.opcodeText)) return false;

	std::string prodNum, prodArr, dstNum, dstArr, srcNum, srcArr;
	if (!splitVectorOperand(producer.rawOperands[0], prodNum, prodArr)) return false;
	if (!splitVectorOperand(copy.rawOperands[0], dstNum, dstArr)) return false;
	if (!splitVectorOperand(copy.rawOperands[1], srcNum, srcArr)) return false;
	if (dstArr != ".16b" || srcArr != ".16b" || srcNum != prodNum) return false;

	if (dstNum == prodNum) {
		block.instrs.erase(block.instrs.begin() + idx + 1);
		return true;
	}

	auto liveIt = liveness.instrLiveOut.find(&block.instrs[idx + 1]);
	if (liveIt == liveness.instrLiveOut.end()) return false;
	for (const auto &def : block.instrs[idx].defs)
		if (liveIt->second.count(def)) return false;

	std::vector<std::string> operands = producer.rawOperands;
	operands[0] = "v" + dstNum + prodArr;
	peephReplaceInstr(block.instrs[idx],
	                  peephMakeInsn(producer.opcodeText, operands));
	block.instrs.erase(block.instrs.begin() + idx + 1);
	return true;
}

static bool canPropagateCopy(const MachineInstr &line,
                             const std::string &tempReg,
                             const std::string &srcReg,
                             std::vector<std::string> &rewrittenOps) {
	auto rewriteUses = [&](std::initializer_list<size_t> useIndices) {
		bool replaced = false;
		for (size_t idx : useIndices) {
			if (idx >= rewrittenOps.size()) continue;
			if (rewrittenOps[idx] != tempReg) continue;
			rewrittenOps[idx] = srcReg;
			replaced = true;
		}
		return replaced;
	};

	rewrittenOps = line.rawOperands;
	if (line.opcodeText == "cmp" || line.opcodeText == "cmn" || line.opcodeText == "fcmp" ||
	    line.opcodeText == "tst" || line.opcodeText == "ccmp") {
		return rewriteUses({0, 1});
	}
	if (line.opcodeText == "str" || line.opcodeText == "stur")
		return rewriteUses({0});
	return false;
}

static bool tryMachineCopyPropagate(MachineBasicBlock &block, size_t idx,
                                    const MachineLivenessResult &liveness) {
	const MachineInstr &copy = block.instrs[idx];
	if (copy.isLabelLike) return false;
	if (copy.opcodeText != "mov" && copy.opcodeText != "fmov") return false;
	if (copy.rawOperands.size() != 2) return false;

	const std::string &tempReg = copy.rawOperands[0];
	const std::string &srcReg = copy.rawOperands[1];
	if (tempReg == srcReg || peephSamePhysicalReg(tempReg, srcReg)) return false;
	if (srcReg == "sp" || srcReg == "wzr" || srcReg == "xzr") return false;

	char tempCls = peephRegClass(tempReg);
	char srcCls = peephRegClass(srcReg);
	if (!tempCls || !srcCls || tempCls != srcCls) return false;
	if (copy.opcodeText == "mov" && (tempCls == 's' || tempCls == 'd')) return false;
	if (copy.opcodeText == "fmov" && tempCls != 's' && tempCls != 'd') return false;

	for (size_t i = idx + 1; i < block.instrs.size(); ++i) {
		const MachineInstr &line = block.instrs[i];
		if (peephIsInertLine(line))
			continue;
		if (line.isLabelLike)
			return false;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;

		if (peephLineWritesReg(line, srcReg))
			return false;
		if (peephLineUsesReg(line, tempReg) && !peephLineReadsReg(line, tempReg))
			return false;
		if (!peephLineReadsReg(line, tempReg)) {
			if (peephLineWritesReg(line, tempReg))
				return false;
			continue;
		}

		std::vector<std::string> rewrittenOps;
		if (!canPropagateCopy(line, tempReg, srcReg, rewrittenOps))
			return false;
		if (!peephRegDeadAfter(block, i, tempReg, liveness))
			return false;

		peephReplaceInstr(block.instrs[i],
		                  peephMakeInsn(line.opcodeText, rewrittenOps));
		block.instrs.erase(block.instrs.begin() + idx);
		return true;
	}

	return false;
}

bool runMachineCopyPropagation(MachineFunction &func) {
	MachineLivenessResult liveness = MachineLiveness().analyze(func);
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		for (size_t i = 0; i < func.blocks[b].instrs.size(); ++i) {
			if (tryMachineSwapMov(func.blocks[b], i) ||
			    tryMachineForwardMov(func.blocks[b], i, liveness) ||
			    tryMachineRetargetCopyDest(func.blocks[b], i, liveness) ||
			    tryMachineRetargetVectorCopyDest(func.blocks[b], i, liveness) ||
			    tryMachineCopyPropagate(func.blocks[b], i, liveness))
				return true;
		}
	}
	return false;
}
