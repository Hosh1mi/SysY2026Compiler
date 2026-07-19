#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cstdlib>
#include <string>
#include <vector>

static constexpr int kScratchRegMin = 10;
static constexpr int kScratchRegMax = 15;
static constexpr int kAddSubImm12Max = 4095;

static bool isScratchReg(const std::string &r) {
	if (r.size() < 2) return false;
	if (r[0] != 'w' && r[0] != 'x') return false;
	int num = std::atoi(r.c_str() + 1);
	return num >= kScratchRegMin && num <= kScratchRegMax;
}

static bool validAddSubSourceForClass(const std::string &reg, char cls) {
	char sourceCls = peephRegClass(reg);
	if (sourceCls == cls) return true;
	return cls == 'x' && reg == "sp";
}

static bool tryMachineImmediateFold(MachineBasicBlock &block, size_t idx,
                                    const MachineLivenessResult &liveness) {
	const MachineInstr &movz = block.instrs[idx];
	if (movz.isLabelLike) return false;
	if (movz.opcodeText != "movz" && movz.opcodeText != "mov") return false;
	if (movz.asmOperands.size() != 2) return false;

	std::string tempReg = movz.asmOperands[0];
	if (!isScratchReg(tempReg)) return false;
	char cls = peephRegClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;

	const std::string &imm = movz.asmOperands[1];
	if (imm.empty() || imm[0] != '#') return false;
	int value = std::atoi(imm.c_str() + 1);
	if (value < 0 || value > kAddSubImm12Max) return false;

	auto window = peephInstrWindow(block, idx + 1, 2);
	if (window.size() < 2) return false;

	size_t aluIdx = window[0];
	size_t movIdx = window[1];
	const MachineInstr &alu = block.instrs[aluIdx];
	const MachineInstr &outMov = block.instrs[movIdx];

	if (alu.opcodeText != "add" && alu.opcodeText != "sub") return false;
	if (alu.asmOperands.size() != 3) return false;

	std::string middleReg = alu.asmOperands[0];
	if (!isScratchReg(middleReg)) return false;
	if (peephRegClass(middleReg) != cls) return false;

	std::string sourceReg;
	if (alu.asmOperands[2] == tempReg) {
		sourceReg = alu.asmOperands[1];
	} else if (alu.opcodeText == "add" && alu.asmOperands[1] == tempReg) {
		sourceReg = alu.asmOperands[2];
	} else {
		return false;
	}
	if (sourceReg == tempReg) return false;
	if (!validAddSubSourceForClass(sourceReg, cls)) return false;

	if (outMov.opcodeText != "mov" || outMov.asmOperands.size() != 2) return false;
	if (outMov.asmOperands[1] != middleReg) return false;
	std::string dstReg = outMov.asmOperands[0];
	if (peephRegClass(dstReg) != cls) return false;

	if (!peephRegDeadAfter(block, movIdx, middleReg, liveness)) return false;
	if (!peephRegDeadAfter(block, movIdx, tempReg, liveness)) return false;

	std::string newImm = "#" + std::to_string(value);
	peephReplaceInstr(block.instrs[movIdx],
	                  peephMakeInsn(alu.opcodeText, {dstReg, sourceReg, newImm}));

	block.instrs.erase(block.instrs.begin() + aluIdx);
	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool tryMachineFoldAddSubMov(MachineBasicBlock &block, size_t idx,
                                    const MachineLivenessResult &liveness) {
	const MachineInstr &alu = block.instrs[idx];
	if (alu.isLabelLike) return false;
	if (alu.opcodeText != "add" && alu.opcodeText != "sub") return false;
	if (alu.asmOperands.size() != 3) return false;

	const std::string &imm = alu.asmOperands[2];
	if (imm.empty() || imm[0] != '#') return false;

	std::string tempReg = alu.asmOperands[0];
	if (!isScratchReg(tempReg)) return false;
	char cls = peephRegClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (!validAddSubSourceForClass(alu.asmOperands[1], cls)) return false;

	auto window = peephInstrWindow(block, idx + 1, 6);
	for (size_t movIdx : window) {
		const MachineInstr &line = block.instrs[movIdx];
		if (line.isLabelLike) continue;
		if (line.isCall) return false;
		if (peephLineWritesReg(line, tempReg)) return false;

		bool isMov = line.opcodeText == "mov" && line.asmOperands.size() == 2;
		if (peephLineUsesReg(line, tempReg) && !isMov) return false;
		if (!isMov || line.asmOperands[1] != tempReg)
			continue;

		std::string dstReg = line.asmOperands[0];
		if (peephRegClass(dstReg) != cls) return false;

		if (dstReg == tempReg) {
			block.instrs.erase(block.instrs.begin() + movIdx);
			return true;
		}

		for (size_t j = idx + 1; j < movIdx; ++j) {
			const MachineInstr &between = block.instrs[j];
			if (between.isLabelLike) continue;
			if (peephLineUsesReg(between, dstReg) || peephLineWritesReg(between, dstReg))
				return false;
		}

		if (!peephRegDeadAfter(block, movIdx, tempReg, liveness)) return false;

		std::vector<std::string> newOperands = alu.asmOperands;
		newOperands[0] = dstReg;
		peephReplaceInstr(block.instrs[idx], peephMakeInsn(alu.opcodeText, newOperands));
		block.instrs.erase(block.instrs.begin() + movIdx);
		return true;
	}

	return false;
}

// Fold a copy that only exists to seed a self-incrementing add/sub:
//
//   mov Xd, Xs
//   ... (Xd not read, Xs and Xd not written)
//   add/sub Xd, Xd, #imm
//
// becomes:
//
//   ...
//   add/sub Xd, Xs, #imm
//
// The move's only consumer is the ALU op, which then overwrites Xd, so once
// the source feeds the add/sub directly the copy is dead. This is the mirror
// of tryMachineFoldAddSubMov for the reversed (copy-before-op) order, e.g. an
// unrolled pointer bump lowered as a snapshot copy plus a constant advance.
static bool tryMachineFoldMovIntoAddSub(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &copy = block.instrs[idx];
	if (copy.isLabelLike) return false;
	if (copy.opcodeText != "mov" || copy.asmOperands.size() != 2) return false;

	const std::string dstReg = copy.asmOperands[0];
	const std::string srcReg = copy.asmOperands[1];
	if (dstReg == srcReg || peephSamePhysicalReg(dstReg, srcReg)) return false;
	char cls = peephRegClass(dstReg);
	if (cls != 'w' && cls != 'x') return false;
	if (peephRegClass(srcReg) != cls) return false;
	if (srcReg == "sp" || srcReg == "wzr" || srcReg == "xzr") return false;

	auto window = peephInstrWindow(block, idx + 1, 6);
	for (size_t aluIdx : window) {
		const MachineInstr &line = block.instrs[aluIdx];
		if (line.isLabelLike) continue;
		if (line.isCall || peephIsControlFlowBarrier(line))
			return false;
		if (peephLineWritesReg(line, srcReg)) return false;

		bool isSelfAddSub =
		    (line.opcodeText == "add" || line.opcodeText == "sub") &&
		    line.asmOperands.size() == 3 &&
		    line.asmOperands[0] == dstReg && line.asmOperands[1] == dstReg &&
		    !line.asmOperands[2].empty() && line.asmOperands[2][0] == '#';
		if (isSelfAddSub) {
			std::vector<std::string> newOperands = line.asmOperands;
			newOperands[1] = srcReg;
			peephReplaceInstr(block.instrs[aluIdx],
			                  peephMakeInsn(line.opcodeText, newOperands));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}
		if (peephLineUsesReg(line, dstReg) || peephLineWritesReg(line, dstReg))
			return false;
	}
	return false;
}

static bool deadAfterConsumer(const MachineBasicBlock &block,
                              size_t consumerIdx,
                              const std::string &removedReg,
                              const std::string &consumerDst,
                              const MachineLivenessResult &liveness) {
	if (peephSamePhysicalReg(removedReg, consumerDst)) return true;
	return peephRegDeadAfter(block, consumerIdx, removedReg, liveness);
}

static bool tryMachineShiftedAddSubFusion(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &shift = block.instrs[idx];
	if (shift.isLabelLike || shift.opcodeText != "lsl" ||
	    shift.asmOperands.size() != 3)
		return false;

	const std::string &shiftDst = shift.asmOperands[0];
	const std::string &shiftSrc = shift.asmOperands[1];
	char cls = peephRegClass(shiftDst);
	if ((cls != 'w' && cls != 'x') || peephRegClass(shiftSrc) != cls)
		return false;

	const std::string &amountText = shift.asmOperands[2];
	if (amountText.size() < 2 || amountText[0] != '#') return false;
	char *end = nullptr;
	long amount = std::strtol(amountText.c_str() + 1, &end, 10);
	long maxAmount = cls == 'w' ? 31 : 63;
	if (!end || *end != '\0' || amount < 0 || amount > maxAmount)
		return false;

	const MachineInstr &consumer = block.instrs[idx + 1];
	if (consumer.isLabelLike ||
	    (consumer.opcodeText != "add" && consumer.opcodeText != "sub") ||
	    consumer.asmOperands.size() != 3)
		return false;

	const std::string &dst = consumer.asmOperands[0];
	const std::string &lhs = consumer.asmOperands[1];
	const std::string &rhs = consumer.asmOperands[2];
	if (peephRegClass(dst) != cls) return false;

	std::string base;
	if (consumer.opcodeText == "add") {
		if (peephSamePhysicalReg(lhs, shiftDst))
			base = rhs;
		else if (peephSamePhysicalReg(rhs, shiftDst))
			base = lhs;
		else
			return false;
	} else {
		if (!peephSamePhysicalReg(rhs, shiftDst)) return false;
		base = lhs;
	}

	if (peephRegClass(base) != cls || peephSamePhysicalReg(base, shiftDst))
		return false;
	if (!peephSamePhysicalReg(shiftDst, dst)) {
		auto liveIt = liveness.instrLiveOut.find(&block.instrs[idx + 1]);
		if (liveIt == liveness.instrLiveOut.end() || block.instrs[idx].defs.empty())
			return false;
		for (const auto &def : block.instrs[idx].defs)
			if (liveIt->second.count(def)) return false;
	}

	peephReplaceInstr(block.instrs[idx + 1],
	                  peephMakeInsn(consumer.opcodeText,
	                                {dst, base, shiftSrc,
	                                 "lsl #" + std::to_string(amount)}));
	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool tryMachineMulAddFusion(MachineBasicBlock &block, size_t idx,
                                   const MachineLivenessResult &liveness) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &mul = block.instrs[idx];
	if (mul.isLabelLike) return false;
	if (mul.opcodeText != "mul" || mul.asmOperands.size() != 3) return false;

	const std::string &mulDst = mul.asmOperands[0];
	const std::string &mulOp1 = mul.asmOperands[1];
	const std::string &mulOp2 = mul.asmOperands[2];
	char cls = peephRegClass(mulDst);
	if (cls != 'w' && cls != 'x') return false;
	if (peephRegClass(mulOp1) != cls || peephRegClass(mulOp2) != cls) return false;

	size_t consumerIdx = idx + 1;
	std::string effectiveSrc = mulDst;
	std::string forwardedReg;

	const MachineInstr *consumerPtr = &block.instrs[consumerIdx];
	if (consumerPtr->isLabelLike) return false;
	if (consumerPtr->opcodeText == "mov" && consumerPtr->asmOperands.size() == 2 &&
	    consumerPtr->asmOperands[1] == mulDst && peephRegClass(consumerPtr->asmOperands[0]) == cls) {
		forwardedReg = consumerPtr->asmOperands[0];
		if (peephSamePhysicalReg(forwardedReg, mulOp1) ||
		    peephSamePhysicalReg(forwardedReg, mulOp2))
			return false;
		if (idx + 2 >= block.instrs.size()) return false;
		consumerIdx = idx + 2;
		effectiveSrc = forwardedReg;
		consumerPtr = &block.instrs[consumerIdx];
		if (consumerPtr->isLabelLike) return false;
	}
	const MachineInstr &consumer = *consumerPtr;

	if (consumer.opcodeText != "add" && consumer.opcodeText != "sub") return false;
	if (consumer.asmOperands.size() != 3) return false;

	const std::string &dst = consumer.asmOperands[0];
	const std::string &lhs = consumer.asmOperands[1];
	const std::string &rhs = consumer.asmOperands[2];
	if (peephRegClass(dst) != cls) return false;

	std::string replacementMnemonic;
	std::vector<std::string> replacementOperands;

	if (consumer.opcodeText == "add") {
		std::string acc;
		if (lhs == effectiveSrc && peephRegClass(rhs) == cls) {
			acc = rhs;
		} else if (rhs == effectiveSrc && peephRegClass(lhs) == cls) {
			acc = lhs;
		} else {
			return false;
		}
		if (peephSamePhysicalReg(acc, effectiveSrc)) return false;
		replacementMnemonic = "madd";
		replacementOperands = {dst, mulOp1, mulOp2, acc};
	} else {
		if (rhs != effectiveSrc) return false;
		if (lhs == "wzr" || lhs == "xzr") {
			replacementMnemonic = "mneg";
			replacementOperands = {dst, mulOp1, mulOp2};
		} else if (peephRegClass(lhs) == cls) {
			if (peephSamePhysicalReg(lhs, effectiveSrc)) return false;
			replacementMnemonic = "msub";
			replacementOperands = {dst, mulOp1, mulOp2, lhs};
		} else {
			return false;
		}
	}

	if (!deadAfterConsumer(block, consumerIdx, mulDst, dst, liveness)) return false;
	if (!forwardedReg.empty() &&
	    !deadAfterConsumer(block, consumerIdx, forwardedReg, dst, liveness))
		return false;

	peephReplaceInstr(block.instrs[consumerIdx],
	                  peephMakeInsn(replacementMnemonic, replacementOperands));
	if (!forwardedReg.empty()) {
		block.instrs.erase(block.instrs.begin() + idx + 1);
		block.instrs.erase(block.instrs.begin() + idx);
	} else {
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}

bool runMachineInstructionCombine(MachineFunction &func,
                                  const MachineLivenessResult &liveness) {
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineImmediateFold(block, i, liveness) ||
			    tryMachineFoldAddSubMov(block, i, liveness) ||
			    tryMachineFoldMovIntoAddSub(block, i) ||
			    tryMachineShiftedAddSubFusion(block, i, liveness) ||
			    tryMachineMulAddFusion(block, i, liveness))
				return true;
		}
	}
	return false;
}
