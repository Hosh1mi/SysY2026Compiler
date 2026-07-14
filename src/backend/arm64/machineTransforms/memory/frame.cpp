#include "frame.hpp"

#include "../../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cstdlib>
#include <string>
#include <vector>

bool tryMachineZeroStore(MachineBasicBlock &block, size_t idx,
                         const MachineLivenessResult &liveness) {
	auto &inst = block.instrs[idx];
	const MachineInstr &zero = inst;
	if (zero.isLabelLike) return false;
	if (zero.opcodeText != "movz" && zero.opcodeText != "mov") return false;
	if (zero.rawOperands.empty()) return false;

	std::string zeroedReg = zero.rawOperands[0];
	char cls = peephRegClass(zeroedReg);
	if (cls != 'w' && cls != 'x') return false;

	bool isZeroing = false;
	if (zero.opcodeText == "movz" && zero.rawOperands.size() >= 2) {
		isZeroing = zero.rawOperands[1] == "#0";
	} else if (zero.opcodeText == "mov" && zero.rawOperands.size() >= 2) {
		const std::string &src = zero.rawOperands[1];
		isZeroing = src == "wzr" || src == "xzr" || src == "#0";
	}
	if (!isZeroing) return false;

	std::string architecturalZero = cls == 'w' ? "wzr" : "xzr";
	bool foundStore = false;
	size_t lastTouched = idx;
	int seen = 0;

	for (size_t i = idx + 1; i < block.instrs.size() && seen < 3; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike) continue;
		++seen;

		if (line.isCall) return false;
		if (peephLineWritesReg(line, zeroedReg)) return false;

		if (line.opcodeText == "str" && line.rawOperands.size() >= 2 &&
		    line.rawOperands[0] == zeroedReg) {
			auto newOps = line.rawOperands;
			newOps[0] = architecturalZero;
			peephReplaceInstr(block.instrs[i], peephMakeInsn("str", newOps));
			foundStore = true;
			lastTouched = i;
			continue;
		}

		if (peephLineUsesReg(line, zeroedReg)) return false;
	}

	if (!foundStore) return false;
	if (!peephRegDeadAfter(block, lastTouched, zeroedReg, liveness)) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

// Forward a store to a later reload from the same known frame slot.  The saved
// register value must remain available, and any intervening store must be
// proven to touch a disjoint frame range.
bool tryMachineStoreLoadForward(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &store = block.instrs[idx];
	if (store.isLabelLike || store.opcodeText != "str" ||
	    store.rawOperands.size() != 2)
		return false;

	MemOperand storeMem = peephParseMemOp(store.rawOperands[1]);
	const std::string &src = store.rawOperands[0];
	char cls = peephRegClass(src);
	const MachineInstr &storeMI = block.instrs[idx];
	if (!storeMem.valid || storeMem.base != "x29" ||
	    !storeMI.memOffsetKnown || storeMI.memBase != "r29" ||
	    storeMI.memWidth <= 0)
		return false;
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;

	auto overlapsStoredSlot = [&](const MachineInstr &mi) {
		if (!mi.memOffsetKnown || mi.memBase != storeMI.memBase || mi.memWidth <= 0)
			return true;
		long long storeBegin = storeMI.memOffset;
		long long storeEnd = storeBegin + storeMI.memWidth;
		long long otherBegin = mi.memOffset;
		long long otherEnd = otherBegin + mi.memWidth;
		return storeBegin < otherEnd && otherBegin < storeEnd;
	};

	for (size_t loadIdx = idx + 1; loadIdx < block.instrs.size(); ++loadIdx) {
		MachineInstr &mi = block.instrs[loadIdx];
		const MachineInstr &line = mi;
		if (line.isLabelLike || mi.isCall || mi.isBarrier)
			return false;

		bool exactSlotLoad = mi.mayLoad && line.opcodeText == "ldr" &&
		                     line.rawOperands.size() == 2 && mi.memOffsetKnown &&
		                     mi.memBase == storeMI.memBase &&
		                     mi.memOffset == storeMI.memOffset &&
		                     mi.memWidth == storeMI.memWidth;
		if (exactSlotLoad) {
			const std::string &dst = line.rawOperands[0];
			if (peephRegClass(dst) != cls) return false;
			if (peephSamePhysicalReg(src, dst)) {
				block.instrs.erase(block.instrs.begin() + loadIdx);
				return true;
			}

			std::string movMnemonic =
			    (cls == 's' || cls == 'd') ? "fmov" : "mov";
			peephReplaceInstr(mi, peephMakeInsn(movMnemonic, {dst, src}));
			return true;
		}

		// A write to the saved value makes the old register value unavailable.
		// Check this after a matching load so `str w9; ...; ldr w9` can simply
		// discard the reload.
		if (peephLineWritesReg(line, src)) return false;

		// Spill slots are private to this frame, but an unclassified store may
		// itself use a frame-derived scratch address.  Only step over stores whose
		// precise frame range is known and disjoint.
		if (mi.mayStore && overlapsStoredSlot(mi)) return false;
	}

	return false;
}

// Pair load/store offsets use a signed 7-bit scaled immediate.
static bool validPairOffset(int offset, int stride) {
	if (stride <= 0) return false;
	if (offset % stride != 0) return false;
	int scaled = offset / stride;
	return scaled >= -64 && scaled <= 63;
}

bool tryMachineDeadStore(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &first = block.instrs[idx];
	const MachineInstr &second = block.instrs[idx + 1];
	if (first.isLabelLike || second.isLabelLike)
		return false;
	if (first.opcodeText != "str" || second.opcodeText != "str") return false;
	if (first.rawOperands.size() != 2 || second.rawOperands.size() != 2) return false;

	MemOperand firstMem = peephParseMemOp(first.rawOperands[1]);
	MemOperand secondMem = peephParseMemOp(second.rawOperands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;
	if (firstMem.offset != secondMem.offset) return false;

	char firstClass = peephRegClass(first.rawOperands[0]);
	if (firstClass != 'w' && firstClass != 'x' && firstClass != 's' && firstClass != 'd')
		return false;
	if (peephRegClass(second.rawOperands[0]) != firstClass) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

bool tryMachineMergeStores(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &first = block.instrs[idx];
	const MachineInstr &second = block.instrs[idx + 1];
	if (first.isLabelLike || second.isLabelLike)
		return false;
	if (first.opcodeText != "str" || second.opcodeText != "str") return false;
	if (first.rawOperands.size() != 2 || second.rawOperands.size() != 2) return false;

	MemOperand firstMem = peephParseMemOp(first.rawOperands[1]);
	MemOperand secondMem = peephParseMemOp(second.rawOperands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;

	char cls = peephRegClass(first.rawOperands[0]);
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;
	if (peephRegClass(second.rawOperands[0]) != cls) return false;

	int stride = peephRegSize(cls);
	int diff = secondMem.offset - firstMem.offset;
	if (std::abs(diff) != stride) return false;

	bool firstIsLower = diff > 0;
	int lowerOffset = firstIsLower ? firstMem.offset : secondMem.offset;
	if (!validPairOffset(lowerOffset, stride)) return false;

	std::string lowerReg = firstIsLower ? first.rawOperands[0] : second.rawOperands[0];
	std::string higherReg = firstIsLower ? second.rawOperands[0] : first.rawOperands[0];
	std::string addr = "[x29, #" + std::to_string(lowerOffset) + "]";

	if (firstIsLower) {
		peephReplaceInstr(block.instrs[idx],
		                    peephMakeInsn("stp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx + 1);
	} else {
		peephReplaceInstr(block.instrs[idx + 1],
		                    peephMakeInsn("stp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}

bool tryMachineMergeLoads(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &first = block.instrs[idx];
	const MachineInstr &second = block.instrs[idx + 1];
	if (first.isLabelLike || second.isLabelLike)
		return false;
	if (first.opcodeText != "ldr" || second.opcodeText != "ldr") return false;
	if (first.rawOperands.size() != 2 || second.rawOperands.size() != 2) return false;

	MemOperand firstMem = peephParseMemOp(first.rawOperands[1]);
	MemOperand secondMem = peephParseMemOp(second.rawOperands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;

	const std::string &firstDst = first.rawOperands[0];
	const std::string &secondDst = second.rawOperands[0];
	char cls = peephRegClass(firstDst);
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;
	if (peephRegClass(secondDst) != cls) return false;
	if (peephSamePhysicalReg(firstDst, secondDst)) return false;
	if (peephSamePhysicalReg(firstDst, firstMem.base) ||
	    peephSamePhysicalReg(secondDst, secondMem.base)) return false;

	int stride = peephRegSize(cls);
	int diff = secondMem.offset - firstMem.offset;
	if (std::abs(diff) != stride) return false;

	bool firstIsLower = diff > 0;
	int lowerOffset = firstIsLower ? firstMem.offset : secondMem.offset;
	if (!validPairOffset(lowerOffset, stride)) return false;

	std::string lowerReg = firstIsLower ? firstDst : secondDst;
	std::string higherReg = firstIsLower ? secondDst : firstDst;
	std::string addr = "[x29, #" + std::to_string(lowerOffset) + "]";

	if (firstIsLower) {
		peephReplaceInstr(block.instrs[idx],
		                    peephMakeInsn("ldp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx + 1);
	} else {
		peephReplaceInstr(block.instrs[idx + 1],
		                    peephMakeInsn("ldp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}
