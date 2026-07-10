#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

static bool tryMachineZeroStore(MachineBasicBlock &block, size_t idx,
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
static bool tryMachineStoreLoadForward(MachineBasicBlock &block, size_t idx) {
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


// ── RULE: (and x, 1) == 0 → tbz / (and x, 1) != 0 → tbnz ───────
//
// Patterns matched:
//   and wN, wX, #1  →  cbz wN, label    →  tbz wX, #0 label
//   and wN, wX, #1  →  cbnz wN, label   →  tbnz wX, #0 label
//   and wN, wX, #1  →  tst wN, wN  →  b.eq label  →  tbz wX, #0 label
//   and wN, wX, #1  →  tst wN, wN  →  b.ne label  →  tbnz wX, #0 label
//   and wN, wX, #1  →  cmp wN, #0  →  b.eq label  →  tbz wX, #0 label
//   and wN, wX, #1  →  cmp wN, #0  →  b.ne label  →  tbnz wX, #0 label
//
// Also handles xN/xX (64-bit) registers.


static bool validPairOffset(int offset, int stride) {
	if (stride <= 0) return false;
	if (offset % stride != 0) return false;
	int scaled = offset / stride;
	return scaled >= -64 && scaled <= 63;
}

static bool tryMachineDeadStore(MachineBasicBlock &block, size_t idx) {
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

static bool tryMachineMergeStores(MachineBasicBlock &block, size_t idx) {
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

static bool tryMachineMergeLoads(MachineBasicBlock &block, size_t idx) {
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

static bool isSimpleNeonMemOp(const MachineInstr &line) {
	return (line.opcodeText == "ld1" || line.opcodeText == "st1") &&
	       line.rawOperands.size() == 2 &&
	       line.rawOperands[0].find('{') != std::string::npos;
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

static bool tryMachinePostIndexScalar(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &mem = block.instrs[idx];
	if (mem.isLabelLike) return false;
	if (mem.opcodeText != "ldr" && mem.opcodeText != "str") return false;
	if (mem.rawOperands.size() != 2) return false;

	char valueClass = peephRegClass(mem.rawOperands[0]);
	if (valueClass != 'w' && valueClass != 'x' && valueClass != 's' &&
	    valueClass != 'd' && valueClass != 'q')
		return false;

	MemOperand addr = peephParseMemOp(mem.rawOperands[1]);
	if (!addr.valid || addr.offset != 0) return false;
	if (peephRegClass(addr.base) != 'x' || addr.base == "sp") return false;
	// Base/data overlap with writeback is constrained-unpredictable for some
	// load/store encodings, so keep those cases in their original form.
	if (peephSamePhysicalReg(mem.rawOperands[0], addr.base)) return false;

	const size_t scanEnd = std::min(block.instrs.size(), idx + 6);
	for (size_t addIdx = idx + 1; addIdx < scanEnd; ++addIdx) {
		const MachineInstr &line = block.instrs[addIdx];
		if (line.isLabelLike) return false;

		if ((line.opcodeText == "add" || line.opcodeText == "sub") &&
		    line.rawOperands.size() == 3 && line.rawOperands[0] == addr.base &&
		    line.rawOperands[1] == addr.base) {
			int amount = 0;
			if (!parseHashImmediate(line.rawOperands[2], amount) || amount <= 0)
				return false;
			int writeback = line.opcodeText == "add" ? amount : -amount;
			if (writeback < -256 || writeback > 255) return false;

			std::vector<std::string> operands = mem.rawOperands;
			operands.push_back("#" + std::to_string(writeback));
			peephReplaceInstr(block.instrs[idx],
			                    peephMakeInsn(mem.opcodeText, operands));
			block.instrs.erase(block.instrs.begin() + addIdx);
			return true;
		}

		// Folding moves the pointer update before intervening instructions.
		// Keep it local to non-trapping instructions independent of the base.
		if (block.instrs[addIdx].mayLoad || block.instrs[addIdx].mayStore ||
		    block.instrs[addIdx].isCall || block.instrs[addIdx].isBarrier ||
		    peephLineUsesReg(line, addr.base))
			return false;
	}
	return false;
}

struct PointerExpr {
	std::string base;
	int offset = 0;
	bool valid = false;
};

static bool parseRegisterNumber(const std::string &reg, char expectedClass, int &num) {
	std::string text = peephTrim(reg);
	if (text.size() < 2 || text[0] != expectedClass)
		return false;
	for (size_t i = 1; i < text.size(); ++i)
		if (!std::isdigit(static_cast<unsigned char>(text[i])))
			return false;
	num = std::atoi(text.c_str() + 1);
	return true;
}

static std::string machineLiveRegName(char cls, int num) {
	if (cls == 'w' || cls == 'x')
		return "r" + std::to_string(num);
	if (cls == 's' || cls == 'd' || cls == 'q' || cls == 'v')
		return "v" + std::to_string(num);
	return "";
}

static PointerExpr memoryExpr(const MemOperand &mem,
                              const std::map<std::string, PointerExpr> &env) {
	PointerExpr out;
	auto it = env.find(mem.base);
	if (it == env.end() || !it->second.valid)
		return out;
	out = it->second;
	out.offset += mem.offset;
	return out;
}

static bool isPlainGPRReg(const std::string &reg, char cls) {
	int num = 0;
	return parseRegisterNumber(reg, cls, num);
}

static bool chooseDeadQReg(const MachineFunction &func,
                           const MachineLivenessResult &liveness,
                           const MachineInstr *before,
                           std::string &qReg) {
	std::set<std::string> live;
	auto liveIt = liveness.instrLiveOut.find(before);
	if (liveIt != liveness.instrLiveOut.end())
		live = liveIt->second;

	std::set<std::string> mentioned;
	for (const auto &block : func.blocks) {
		for (const auto &inst : block.instrs) {
			const MachineInstr &line = inst;
			if (line.isLabelLike)
				continue;
			for (const auto &op : line.rawOperands) {
				char cls = 0;
				std::string num;
				if (peephParsePhysicalReg(op, cls, num) &&
				    (cls == 's' || cls == 'd' || cls == 'q' || cls == 'v'))
					mentioned.insert("v" + num);
				MemOperand mem = peephParseMemOp(op);
				if (mem.valid) {
					char baseCls = 0;
					std::string baseNum;
					if (peephParsePhysicalReg(mem.base, baseCls, baseNum) &&
					    (baseCls == 's' || baseCls == 'd' ||
					     baseCls == 'q' || baseCls == 'v'))
						mentioned.insert("v" + baseNum);
				}
			}
		}
	}

	for (int r = 16; r <= 31; ++r) {
		std::string liveName = "v" + std::to_string(r);
		if (live.count(liveName) || mentioned.count(liveName))
			continue;
		qReg = "q" + std::to_string(r);
		return true;
	}
	for (int r = 0; r <= 7; ++r) {
		std::string liveName = "v" + std::to_string(r);
		if (live.count(liveName) || mentioned.count(liveName))
			continue;
		qReg = "q" + std::to_string(r);
		return true;
	}
	return false;
}

static bool tryMachineWidenI32CopyWindow(MachineFunction &func,
                                         MachineBasicBlock &block,
                                         size_t idx,
                                         const MachineLivenessResult &liveness) {
	if (idx >= block.instrs.size())
		return false;

	const MachineInstr &first = block.instrs[idx];
	if (first.isLabelLike || first.opcodeText != "ldr" ||
	    first.rawOperands.size() != 2 || !isPlainGPRReg(first.rawOperands[0], 'w'))
		return false;

	MemOperand firstMem = peephParseMemOp(first.rawOperands[1]);
	if (!firstMem.valid || firstMem.base == "sp")
		return false;

	const size_t MaxScan = 32;
	const size_t end = std::min(block.instrs.size(), idx + MaxScan);

	std::map<std::string, PointerExpr> env;
	for (size_t i = idx; i < end; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike)
			break;
		for (const auto &op : line.rawOperands) {
			MemOperand mem = peephParseMemOp(op);
			if (mem.valid && peephRegClass(mem.base) == 'x' && !env.count(mem.base))
				env[mem.base] = {mem.base, 0, true};
		}
		if ((line.opcodeText == "mov" || line.opcodeText == "add") &&
		    !line.rawOperands.empty() && peephRegClass(line.rawOperands[0]) == 'x' &&
		    !env.count(line.rawOperands[0]))
			env[line.rawOperands[0]] = {line.rawOperands[0], 0, true};
	}
	if (!env.count(firstMem.base))
		env[firstMem.base] = {firstMem.base, 0, true};

	struct CopyPair {
		int offset;
	};
	std::map<std::string, PointerExpr> loadValue;
	std::vector<CopyPair> copies;

	std::string srcBase;
	std::string dstBase;
	size_t replaceEnd = idx;
	bool complete = false;
	std::string counterReg;
	bool sawCounterInc = false;

	auto gprAlias = [](const std::string &reg) -> std::string {
		if (reg.size() < 2 || !std::isdigit(static_cast<unsigned char>(reg[1])))
			return "";
		if (reg[0] == 'w')
			return "x" + reg.substr(1);
		if (reg[0] == 'x')
			return "w" + reg.substr(1);
		return "";
	};

	auto invalidateReg = [&](const std::string &reg) {
		env.erase(reg);
		loadValue.erase(reg);
		std::string alias = gprAlias(reg);
		if (!alias.empty()) {
			env.erase(alias);
			loadValue.erase(alias);
		}
	};

	auto findExprReg = [&](const std::string &base,
	                       const std::set<std::string> *preferredLive,
	                       std::string &regOut) {
		std::string fallback;
		for (const auto &entry : env) {
			if (peephRegClass(entry.first) != 'x' || !entry.second.valid ||
			    entry.second.base != base || entry.second.offset != 16)
				continue;
			int regNo = 0;
			if (!parseRegisterNumber(entry.first, 'x', regNo))
				continue;
			std::string liveName = "r" + std::to_string(regNo);
			if (preferredLive && preferredLive->count(liveName)) {
				regOut = entry.first;
				return true;
			}
			if (entry.first == base)
				fallback = entry.first;
			else if (fallback.empty())
				fallback = entry.first;
		}
		if (!fallback.empty()) {
			regOut = fallback;
			return true;
		}
		return false;
	};

	auto hasFinalPointerUpdates = [&]() {
		if (copies.size() != 4 || srcBase.empty() || dstBase.empty())
			return false;
		std::string srcReg;
		std::string dstReg;
		return findExprReg(srcBase, nullptr, srcReg) &&
		       findExprReg(dstBase, nullptr, dstReg);
	};

	for (size_t i = idx; i < end; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike)
			break;
		if (line.opcodeText == "b" || line.opcodeText == "bl" || line.opcodeText == "blr" ||
		    line.opcodeText == "ret" || line.opcodeText == "cmp" || line.opcodeText == "tst" ||
		    line.opcodeText == "cbz" || line.opcodeText == "cbnz" ||
		    line.opcodeText == "tbz" || line.opcodeText == "tbnz")
			break;

		if (line.opcodeText == "mov" && line.rawOperands.size() == 2 &&
		    peephRegClass(line.rawOperands[0]) == 'x' && peephRegClass(line.rawOperands[1]) == 'x') {
			auto srcIt = env.find(line.rawOperands[1]);
			if (srcIt == env.end() || !srcIt->second.valid)
				return false;
			invalidateReg(line.rawOperands[0]);
			env[line.rawOperands[0]] = srcIt->second;
			if (hasFinalPointerUpdates()) {
				replaceEnd = i;
				complete = true;
				break;
			}
			continue;
		}

		if (line.opcodeText == "add" && line.rawOperands.size() == 3 &&
		    peephRegClass(line.rawOperands[0]) == 'x' && peephRegClass(line.rawOperands[1]) == 'x' &&
		    !line.rawOperands[2].empty() && line.rawOperands[2][0] == '#') {
			auto srcIt = env.find(line.rawOperands[1]);
			if (srcIt == env.end() || !srcIt->second.valid)
				return false;
			int amount = 0;
			if (!parseHashImmediate(line.rawOperands[2], amount))
				return false;
			invalidateReg(line.rawOperands[0]);
			env[line.rawOperands[0]] = {srcIt->second.base,
			                         srcIt->second.offset + amount,
			                         true};
			if (hasFinalPointerUpdates()) {
				replaceEnd = i;
				complete = true;
				break;
			}
			continue;
		}

		if (line.opcodeText == "add" && line.rawOperands.size() == 3 &&
		    peephRegClass(line.rawOperands[0]) == 'w' && line.rawOperands[0] == line.rawOperands[1] &&
		    line.rawOperands[2] == "#4") {
			if (sawCounterInc)
				return false;
			sawCounterInc = true;
			counterReg = line.rawOperands[0];
			invalidateReg(counterReg);
			if (hasFinalPointerUpdates()) {
				replaceEnd = i;
				complete = true;
				break;
			}
			continue;
		}

		if (line.opcodeText == "ldr" &&
		    (line.rawOperands.size() == 2 || line.rawOperands.size() == 3) &&
		    isPlainGPRReg(line.rawOperands[0], 'w')) {
			MemOperand mem = peephParseMemOp(line.rawOperands[1]);
			if (!mem.valid)
				return false;
			int postInc = 0;
			if (line.rawOperands.size() == 3 &&
			    !parseHashImmediate(line.rawOperands[2], postInc))
				return false;
			PointerExpr expr = memoryExpr(mem, env);
			if (!expr.valid)
				return false;
			if (srcBase.empty()) {
				srcBase = expr.base;
			} else if (srcBase != expr.base) {
				return false;
			}
			invalidateReg(line.rawOperands[0]);
			loadValue[line.rawOperands[0]] = expr;
			if (line.rawOperands.size() == 3)
				env[mem.base] = {expr.base, expr.offset + postInc, true};
			if (hasFinalPointerUpdates()) {
				replaceEnd = i;
				complete = true;
				break;
			}
			continue;
		}

		if (line.opcodeText == "str" &&
		    (line.rawOperands.size() == 2 || line.rawOperands.size() == 3) &&
		    isPlainGPRReg(line.rawOperands[0], 'w')) {
			auto valIt = loadValue.find(line.rawOperands[0]);
			if (valIt == loadValue.end() || !valIt->second.valid)
				return false;
			MemOperand mem = peephParseMemOp(line.rawOperands[1]);
			if (!mem.valid)
				return false;
			int postInc = 0;
			if (line.rawOperands.size() == 3 &&
			    !parseHashImmediate(line.rawOperands[2], postInc))
				return false;
			PointerExpr dst = memoryExpr(mem, env);
			if (!dst.valid)
				return false;
			if (dstBase.empty()) {
				dstBase = dst.base;
			} else if (dstBase != dst.base) {
				return false;
			}
			if (dst.offset != valIt->second.offset)
				return false;
			copies.push_back({dst.offset});
			if (line.rawOperands.size() == 3)
				env[mem.base] = {dst.base, dst.offset + postInc, true};
			if (hasFinalPointerUpdates()) {
				replaceEnd = i;
				complete = true;
				break;
			}
			continue;
		}

		if (block.instrs[i].mayLoad || block.instrs[i].mayStore ||
		    block.instrs[i].isCall || block.instrs[i].isBarrier)
			return false;

		if (!line.rawOperands.empty()) {
			char cls = peephRegClass(line.rawOperands[0]);
			if (cls == 'w' || cls == 'x')
				invalidateReg(line.rawOperands[0]);
		}
		if (hasFinalPointerUpdates()) {
			replaceEnd = i;
			complete = true;
			break;
		}
	}

	if (!complete || copies.size() != 4 ||
	    srcBase.empty() || dstBase.empty() || srcBase == dstBase)
		return false;
	std::set<int> offsets;
	for (const auto &copy : copies)
		offsets.insert(copy.offset);
	if (!offsets.count(0) || !offsets.count(4) ||
	    !offsets.count(8) || !offsets.count(12))
		return false;

	auto liveAfterIt = liveness.instrLiveOut.find(&block.instrs[replaceEnd]);
	if (liveAfterIt == liveness.instrLiveOut.end())
		return false;
	const auto &liveAfter = liveAfterIt->second;
	std::string srcUpdateReg;
	std::string dstUpdateReg;
	if (!findExprReg(srcBase, &liveAfter, srcUpdateReg) ||
	    !findExprReg(dstBase, &liveAfter, dstUpdateReg))
		return false;

	for (size_t i = idx; i <= replaceEnd; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike || line.rawOperands.empty())
			continue;
		char cls = peephRegClass(line.rawOperands[0]);
		if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd' &&
		    cls != 'q' && cls != 'v')
			continue;
		std::string liveName;
		int regNo = 0;
		if (parseRegisterNumber(line.rawOperands[0], cls, regNo))
			liveName = machineLiveRegName(cls, regNo);
		if (liveName.empty())
			continue;
		if (peephSamePhysicalReg(line.rawOperands[0], srcUpdateReg) ||
		    peephSamePhysicalReg(line.rawOperands[0], dstUpdateReg) ||
		    peephSamePhysicalReg(line.rawOperands[0], counterReg))
			continue;
		if (liveAfter.count(liveName))
			return false;
	}

	std::string qReg;
	if (!chooseDeadQReg(func, liveness, &block.instrs[idx], qReg))
		return false;

	std::vector<MachineInstr> repl;
	auto append = [&](const std::string &text) {
		MachineInstr inst = parseMachineInstr(text, block.instrs[idx].originalIndex);
		repl.push_back(std::move(inst));
	};
	append("\tldr " + qReg + ", [" + srcBase + "]");
	append("\tstr " + qReg + ", [" + dstBase + "]");
	append("\tadd " + srcUpdateReg + ", " + srcBase + ", #16");
	append("\tadd " + dstUpdateReg + ", " + dstBase + ", #16");
	if (sawCounterInc)
		append("\tadd " + counterReg + ", " + counterReg + ", #4");

	block.instrs.erase(block.instrs.begin() + idx,
	                   block.instrs.begin() + replaceEnd + 1);
	block.instrs.insert(block.instrs.begin() + idx,
	                    std::make_move_iterator(repl.begin()),
	                    std::make_move_iterator(repl.end()));
	return true;
}

static bool tryMachinePostIndexNeon(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	const MachineInstr &mem = block.instrs[idx];
	if (mem.isLabelLike)
		return false;
	if (!isSimpleNeonMemOp(mem)) return false;

	MemOperand addr = peephParseMemOp(mem.rawOperands[1]);
	if (!addr.valid || addr.offset != 0) return false;
	if (addr.base.empty() || addr.base == "sp") return false;

	std::string postBase = addr.base;
	{
		size_t begin = idx > 4 ? idx - 4 : 0;
		for (size_t copyIdx = idx; copyIdx-- > begin;) {
			const MachineInstr &copy = block.instrs[copyIdx];
			if (copy.isLabelLike)
				break;
			if (copy.opcodeText != "mov" || copy.rawOperands.size() != 2 ||
			    copy.rawOperands[0] != addr.base || peephRegClass(copy.rawOperands[1]) != 'x')
				continue;

			bool clobbered = false;
			for (size_t j = copyIdx + 1; j < idx; ++j) {
				const MachineInstr &between = block.instrs[j];
				if (between.isLabelLike ||
				    block.instrs[j].isCall || block.instrs[j].isBarrier ||
				    peephLineWritesReg(between, addr.base) ||
				    peephLineWritesReg(between, copy.rawOperands[1])) {
					clobbered = true;
					break;
				}
			}
			if (!clobbered) {
				postBase = copy.rawOperands[1];
				break;
			}
		}
	}
	if (postBase.empty() || postBase == "sp") return false;

	size_t addIdx = idx + 1;
	bool foundAdd = false;
	const size_t scanEnd = std::min(block.instrs.size(), idx + 6);
	for (; addIdx < scanEnd; ++addIdx) {
		const MachineInstr &line = block.instrs[addIdx];
		if (line.isLabelLike)
			return false;
		if (line.opcodeText == "add" && line.rawOperands.size() == 3 &&
		    line.rawOperands[0] == postBase && line.rawOperands[1] == postBase &&
		    line.rawOperands[2] == "#16") {
			foundAdd = true;
			break;
		}
		if (block.instrs[addIdx].mayLoad || block.instrs[addIdx].mayStore ||
		    block.instrs[addIdx].isCall || block.instrs[addIdx].isBarrier ||
		    peephLineUsesReg(line, postBase))
			return false;
	}
	if (!foundAdd)
		return false;

	std::vector<std::string> operands = mem.rawOperands;
	operands[1] = "[" + postBase + "]";
	operands.push_back("#16");
	peephReplaceInstr(block.instrs[idx],
	                    peephMakeInsn(mem.opcodeText, operands));
	block.instrs.erase(block.instrs.begin() + addIdx);
	return true;
}

bool runMachineMemoryOptimization(MachineFunction &func) {
	MachineLivenessResult liveness = MachineLiveness().analyze(func);
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineWidenI32CopyWindow(func, block, i, liveness) ||
			    tryMachineStoreLoadForward(block, i) ||
			    tryMachineZeroStore(block, i, liveness) ||
			    tryMachineDeadStore(block, i) ||
			    tryMachinePostIndexScalar(block, i) ||
			    tryMachinePostIndexNeon(block, i) ||
			    tryMachineMergeStores(block, i) ||
			    tryMachineMergeLoads(block, i))
				return true;
		}
	}
	return false;
}
