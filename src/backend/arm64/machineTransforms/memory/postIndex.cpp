#include "postIndex.hpp"

#include "../../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

static bool isSimpleNeonMemOp(const MachineInstr &line) {
	return (line.opcodeText == "ld1" || line.opcodeText == "st1") &&
	       line.asmOperands.size() == 2 &&
	       line.asmOperands[0].find('{') != std::string::npos;
}

static bool parseHashImmediate(const std::string &operand, int &value) {
	std::string text = peephTrim(operand);
	if (text.size() < 2 || text[0] != '#') return false;
	char *end = nullptr;
	long parsed = std::strtol(text.c_str() + 1, &end, 0);
	if (!end || *end != '\0') return false;
	if (parsed < std::numeric_limits<int>::min() ||
	    parsed > std::numeric_limits<int>::max())
		return false;
	value = static_cast<int>(parsed);
	return true;
}

static bool hasEarlierLoadFromBase(const MachineBasicBlock &block, size_t idx,
                                   const std::string &base) {
	for (size_t i = idx; i-- > 0;) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike || line.isCall || line.isBarrier) return false;
		if (peephLineWritesReg(line, base)) return false;
		if (!line.mayLoad) continue;
		for (const std::string &operand : line.asmOperands) {
			MemOperand addr = peephParseMemOp(operand);
			if (addr.valid && peephSamePhysicalReg(addr.base, base)) return true;
		}
	}
	return false;
}

bool tryMachinePostIndexScalar(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &mem = block.instrs[idx];
	if (mem.isLabelLike) return false;
	if (mem.opcodeText != "ldr" && mem.opcodeText != "str") return false;
	if (mem.asmOperands.size() != 2) return false;

	char valueClass = peephRegClass(mem.asmOperands[0]);
	if (valueClass != 'w' && valueClass != 'x' && valueClass != 's' &&
	    valueClass != 'd' && valueClass != 'q')
		return false;
	MemOperand addr = peephParseMemOp(mem.asmOperands[1]);
	if (!addr.valid || addr.offset != 0) return false;
	if (peephRegClass(addr.base) != 'x' || addr.base == "sp") return false;
	// Cortex-A53 executes in order.  For a read-modify-write stream, updating
	// the loop-carried address from the vector store makes the following
	// iteration's load wait for store writeback.  Keep the explicit integer
	// add independent of the store; loads still benefit from post-indexing.
	if (mem.opcodeText == "str" && valueClass == 'q' &&
	    hasEarlierLoadFromBase(block, idx, addr.base))
		return false;
	// Base/data overlap with writeback is constrained-unpredictable for some
	// load/store encodings, so keep those cases in their original form.
	if (peephSamePhysicalReg(mem.asmOperands[0], addr.base)) return false;

	const size_t scanEnd = std::min(block.instrs.size(), idx + 6);
	for (size_t addIdx = idx + 1; addIdx < scanEnd; ++addIdx) {
		const MachineInstr &line = block.instrs[addIdx];
		if (line.isLabelLike) return false;

		if ((line.opcodeText == "add" || line.opcodeText == "sub") &&
		    line.asmOperands.size() == 3 && line.asmOperands[0] == addr.base &&
		    line.asmOperands[1] == addr.base) {
			int amount = 0;
			if (!parseHashImmediate(line.asmOperands[2], amount) || amount <= 0)
				return false;
			int writeback = line.opcodeText == "add" ? amount : -amount;
			if (writeback < -256 || writeback > 255) return false;

			std::vector<std::string> operands = mem.asmOperands;
			operands.push_back("#" + std::to_string(writeback));
			peephReplaceInstr(block.instrs[idx],
			                    peephMakeInsn(mem.opcodeText, operands));
			block.instrs.erase(block.instrs.begin() + addIdx);
			return true;
		}

		// Folding moves the pointer update before intervening instructions.
		// Keep it local to non-trapping instructions independent of the base.
		// mayLoad/mayStore alone is not a reason to bail: a load/store to a
		// different address register does not affect the post-index base.
		// peephLineUsesReg catches any instruction that reads or writes
		// addr.base; calls/barriers are still unsafe.
		if (block.instrs[addIdx].isCall || block.instrs[addIdx].isBarrier ||
		    peephLineUsesReg(line, addr.base))
			return false;
	}
	return false;
}

bool tryMachinePostIndexNeon(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &mem = block.instrs[idx];
	if (mem.isLabelLike)
		return false;
	if (!isSimpleNeonMemOp(mem)) return false;
	MemOperand addr = peephParseMemOp(mem.asmOperands[1]);
	if (!addr.valid || addr.offset != 0) return false;
	if (addr.base.empty() || addr.base == "sp") return false;

	std::string postBase = addr.base;
	{
		size_t begin = idx > 4 ? idx - 4 : 0;
		for (size_t copyIdx = idx; copyIdx-- > begin;) {
			const MachineInstr &copy = block.instrs[copyIdx];
			if (copy.isLabelLike)
				break;
			if (copy.opcodeText != "mov" || copy.asmOperands.size() != 2 ||
			    copy.asmOperands[0] != addr.base || peephRegClass(copy.asmOperands[1]) != 'x')
				continue;

			bool clobbered = false;
			for (size_t j = copyIdx + 1; j < idx; ++j) {
				const MachineInstr &between = block.instrs[j];
				if (between.isLabelLike ||
				    block.instrs[j].isCall || block.instrs[j].isBarrier ||
				    peephLineWritesReg(between, addr.base) ||
				    peephLineWritesReg(between, copy.asmOperands[1])) {
					clobbered = true;
					break;
				}
			}
			if (!clobbered) {
				postBase = copy.asmOperands[1];
				break;
			}
		}
	}
	if (postBase.empty() || postBase == "sp") return false;
	// Apply the same read-modify-write policy before ld1/st1 are printed as
	// their ldr/str vector aliases.
	if (mem.opcodeText == "st1" &&
	    (hasEarlierLoadFromBase(block, idx, addr.base) ||
	     (postBase != addr.base &&
	      hasEarlierLoadFromBase(block, idx, postBase))))
		return false;

	size_t addIdx = idx + 1;
	bool foundAdd = false;
	const size_t scanEnd = std::min(block.instrs.size(), idx + 6);
	for (; addIdx < scanEnd; ++addIdx) {
		const MachineInstr &line = block.instrs[addIdx];
		if (line.isLabelLike)
			return false;
		if (line.opcodeText == "add" && line.asmOperands.size() == 3 &&
		    line.asmOperands[0] == postBase && line.asmOperands[1] == postBase &&
		    line.asmOperands[2] == "#16") {
			foundAdd = true;
			break;
		}
		// mayLoad/mayStore alone is not a reason to bail: a load/store to a
		// different address register does not affect the post-index base.
		// peephLineUsesReg catches any instruction that reads or writes
		// postBase; calls/barriers are still unsafe.
		if (block.instrs[addIdx].isCall || block.instrs[addIdx].isBarrier ||
		    peephLineUsesReg(line, postBase))
			return false;
	}
	if (!foundAdd)
		return false;

	std::vector<std::string> operands = mem.asmOperands;
	operands[1] = "[" + postBase + "]";
	operands.push_back("#16");
	peephReplaceInstr(block.instrs[idx],
	                    peephMakeInsn(mem.opcodeText, operands));
	block.instrs.erase(block.instrs.begin() + addIdx);
	return true;
}
