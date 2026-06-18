#include "../../include/backend/arm64/peephole.hpp"
#include "../../include/backend/arm64/liveness.hpp"
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

// ── parsing helpers ──────────────────────────────────────────────────

enum class LineKind { Instruction, Label, Directive, Empty, Comment };

struct ParsedLine {
	std::string raw;                // original text with trailing '\n'
	LineKind kind = LineKind::Instruction;
	std::string mnemonic;           // lowercase
	std::vector<std::string> operands; // split by comma, bracket-aware
};

struct MemOperand {
	std::string base;
	int offset = 0;
	bool valid = false;
};

static std::string trim(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n')) --e;
	return s.substr(b, e - b);
}

static char regClass(const std::string &r) {
	if (r.empty()) return 0;
	if (r == "wzr") return 'w';
	if (r == "xzr") return 'x';
	if (r == "sp") return 'x';
	char c = r[0];
	if ((c == 'w' || c == 'x' || c == 's' || c == 'd') && r.size() >= 2 && std::isdigit(r[1]))
		return c;
	return 0;
}

static int regSize(char cls) {
	if (cls == 'w' || cls == 's') return 4;
	if (cls == 'x' || cls == 'd') return 8;
	return 0;
}

static bool samePhysicalReg(const std::string &a, const std::string &b) {
	if (a == b) return true;
	if (a.size() < 2 || b.size() < 2) return false;

	char aCls = a[0];
	char bCls = b[0];
	bool sameIntFile = (aCls == 'w' || aCls == 'x') && (bCls == 'w' || bCls == 'x');
	bool sameFloatFile = (aCls == 's' || aCls == 'd') && (bCls == 's' || bCls == 'd');
	if (!sameIntFile && !sameFloatFile) return false;

	if (!std::isdigit(a[1]) || !std::isdigit(b[1])) return false;
	return a.substr(1) == b.substr(1);
}

// Parse [base, #offset], [base], [base, #-offset]
// Rejects post-index ([base], #imm) and pre-index ([base, #imm]!)
static MemOperand parseMemOp(const std::string &s) {
	MemOperand m;
	std::string t = trim(s);
	if (t.empty() || t[0] != '[') return m;
	size_t close = t.find(']');
	if (close == std::string::npos) return m;
	// Reject post-index and pre-index
	if (close + 1 < t.size()) {
		char after = t[close + 1];
		if (after == '!' || after == ',') return m;
	}
	std::string inner = t.substr(1, close - 1);
	size_t comma = inner.find(',');
	if (comma == std::string::npos) {
		// [base]
		m.base = trim(inner);
		m.offset = 0;
		m.valid = true;
	} else {
		m.base = trim(inner.substr(0, comma));
		std::string offStr = trim(inner.substr(comma + 1));
		if (!offStr.empty() && offStr[0] == '#') {
			m.offset = std::atoi(offStr.c_str() + 1);
			m.valid = true;
		}
	}
	return m;
}

// Split operands string respecting bracket nesting
static std::vector<std::string> splitOperands(const std::string &s) {
	std::vector<std::string> out;
	int depth = 0;
	size_t start = 0;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '[') ++depth;
		else if (s[i] == ']') --depth;
		else if (s[i] == ',' && depth == 0) {
			out.push_back(trim(s.substr(start, i - start)));
			start = i + 1;
		}
	}
	out.push_back(trim(s.substr(start)));
	return out;
}

static ParsedLine parseLine(const std::string &raw) {
	ParsedLine pl;
	pl.raw = raw;
	std::string t = trim(raw);
	if (t.empty()) { pl.kind = LineKind::Empty; return pl; }
	if (t[0] == '#' || (t.size() >= 2 && t[0] == '/' && t[1] == '/')) {
		pl.kind = LineKind::Comment; return pl;
	}
	if (t[t.size() - 1] == ':') { pl.kind = LineKind::Label; return pl; }
	if (t[0] == '.') { pl.kind = LineKind::Directive; return pl; }

	pl.kind = LineKind::Instruction;
	// Extract mnemonic and operands
	size_t sp = t.find_first_of(" \t");
	if (sp == std::string::npos) {
		pl.mnemonic = t;
		return pl;
	}
	pl.mnemonic = t.substr(0, sp);
	// Lowercase the mnemonic
	for (char &c : pl.mnemonic) c = (char)std::tolower(c);
	std::string rest = trim(t.substr(sp + 1));
	if (!rest.empty())
		pl.operands = splitOperands(rest);
	return pl;
}

static std::string makeMachineInsn(const std::string &mnemonic,
                                   const std::vector<std::string> &operands) {
	std::string s = "\t" + mnemonic;
	if (!operands.empty()) {
		s += "\t";
		for (size_t i = 0; i < operands.size(); ++i) {
			if (i > 0) s += ", ";
			s += operands[i];
		}
	}
	return s;
}

static void replaceMachineInstr(MachineInstr &inst, const std::string &text) {
	int originalIndex = inst.originalIndex;
	MachineInstr parsed = parseMachineInstr(text, originalIndex);
	parsed.originalIndex = originalIndex;
	inst = std::move(parsed);
}

// ── helper: count occurrences of a register in a line's operands ────

// Check if r is read as a source operand (not just written as destination)
static bool lineReadsReg(const ParsedLine &l, const std::string &r) {
	if (l.operands.empty()) return false;
	for (size_t i = 1; i < l.operands.size(); ++i)
		if (l.operands[i] == r || samePhysicalReg(l.operands[i], r)) return true;
	if (l.mnemonic == "str" || l.mnemonic == "stp" || l.mnemonic == "stur" ||
	    l.mnemonic == "cbnz" || l.mnemonic == "cbz" ||
	    l.mnemonic == "cmp" || l.mnemonic == "fcmp" || l.mnemonic == "tst")
		if (l.operands[0] == r || samePhysicalReg(l.operands[0], r)) return true;
	return false;
}

static bool lineUsesReg(const ParsedLine &l, const std::string &r) {
	for (const auto &op : l.operands) {
		// Check as whole word match
		if (op == r || samePhysicalReg(op, r)) return true;
		MemOperand mem = parseMemOp(op);
		if (mem.valid && samePhysicalReg(mem.base, r)) return true;
		// Check if r appears inside a memory operand
		size_t p = op.find(r);
		if (p != std::string::npos) {
			if (p > 0 && (std::isalnum(op[p - 1]) || op[p - 1] == '_')) continue;
			size_t after = p + r.size();
			if (after < op.size() && (std::isalnum(op[after]) || op[after] == '_')) continue;
			return true;
		}
	}
	return false;
}

// Does line write (not just read) a register?
static bool lineWritesReg(const ParsedLine &l, const std::string &r) {
	// Stores, compares, branches do not write a register destination
	if (l.mnemonic == "str" || l.mnemonic == "stp" ||
		l.mnemonic == "cmp" || l.mnemonic == "fcmp" ||
		l.mnemonic == "cbnz" || l.mnemonic == "cbz" ||
		l.mnemonic == "b" || l.mnemonic == "bl" || l.mnemonic == "blr" ||
		l.mnemonic == "ret" || l.mnemonic == "st1")
		return false;
	// ldp writes two registers: ldp r0, r1, [...]
	if (l.mnemonic == "ldp") {
		for (int i = 0; i < 2 && i < (int)l.operands.size(); ++i) {
			const std::string &op = l.operands[i];
			if (op == r) return true;
			if (op.size() >= 2 && r.size() >= 2 &&
			    ((op[0] == 'w' || op[0] == 'x') && (r[0] == 'w' || r[0] == 'x')) &&
			    op.substr(1) == r.substr(1)) return true;
		}
		return false;
	}
	if (!l.operands.empty()) {
		const std::string &dst = l.operands[0];
		if (dst == r) return true;
		// ARM64 register aliasing: wN and xN share the same physical register,
		// as do sN and dN. A write to either width clobbers the other.
		if (dst.size() >= 2 && r.size() >= 2) {
			char clsDst = dst[0], clsR = r[0];
			bool sameFile = ((clsDst == 'w' || clsDst == 'x') && (clsR == 'w' || clsR == 'x')) ||
			                ((clsDst == 's' || clsDst == 'd') && (clsR == 's' || clsR == 'd'));
			if (sameFile && dst.substr(1) == r.substr(1)) return true;
		}
	}
	return false;
}

// Call instructions (bl, blr) clobber all caller-saved registers.
// Since scratch regs (w10-w15, x10-x15, s16-s31) are all caller-saved,
// any call between a store and a load makes forwarding unsafe.
static bool isCallBarrier(const std::string &m) {
	return m == "bl" || m == "blr";
}

// ── RULE implementations ────────────────────────────────────────────
// Each returns true if it made a change.

// Scratch register range — must agree with the emitter's caller-saved
// scratch allocator (backend/arm64/load_store.cpp).  w10–w15 / x10–x15 are the
// short-lived temporaries the emitter uses for intermediate values within a
// single materialization (e.g. the movz→add→mov triple that tryImmediateFold
// folds).  If the allocator's scratch range changes, both sides must move.
static constexpr int kScratchRegMin = 10;
static constexpr int kScratchRegMax = 15;

// Add/sub unshifted imm12 range.  The `lsl #12` variant ([4096,
// 16773120]) is intentionally not folded — the emitter does not currently
// generate the fold-eligible pattern for those, and supporting it would
// require a more careful operand-shape check.
static constexpr int kAddSubImm12Max = 4095;

// True for w10-w15 / x10-x15.
static bool isScratchReg(const std::string &r) {
	if (r.size() < 2) return false;
	if (r[0] != 'w' && r[0] != 'x') return false;
	int num = std::atoi(r.c_str() + 1);
	return num >= kScratchRegMin && num <= kScratchRegMax;
}

// ── Main optimization loop ──────────────────────────────────────────

static bool tryMachineSelfMove(MachineBasicBlock &block, size_t idx) {
	auto &inst = block.instrs[idx];
	ParsedLine line = parseLine(inst.text);
	if (line.kind != LineKind::Instruction) return false;
	if (line.mnemonic != "mov" && line.mnemonic != "fmov") return false;
	if (line.operands.size() < 2) return false;
	if (line.operands[0] != line.operands[1]) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool tryMachineSwapMov(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine first = parseLine(block.instrs[idx].text);
	ParsedLine second = parseLine(block.instrs[idx + 1].text);
	if (first.kind != LineKind::Instruction || second.kind != LineKind::Instruction)
		return false;
	if (first.mnemonic != "mov" || second.mnemonic != "mov") return false;
	if (first.operands.size() < 2 || second.operands.size() < 2) return false;

	const std::string &rA = first.operands[0];
	const std::string &rB = first.operands[1];
	if (rA == rB) return false;
	if (rB.empty() || rB[0] == '#') return false;
	if (!regClass(rA) || !regClass(rB)) return false;
	if (second.operands[0] != rB || second.operands[1] != rA) return false;

	block.instrs.erase(block.instrs.begin() + idx + 1);
	return true;
}

static bool machineRegDeadAfter(const MachineBasicBlock &block,
                                size_t idx,
                                const std::string &reg);
static bool isControlFlowBarrier(const std::string &mnemonic);

static bool tryMachineForwardMov(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine first = parseLine(block.instrs[idx].text);
	if (first.kind != LineKind::Instruction) return false;
	if (first.mnemonic != "mov" || first.operands.size() != 2) return false;

	const std::string &tempReg = first.operands[0];
	const std::string &srcReg = first.operands[1];
	if (tempReg == srcReg) return false;
	if (!regClass(tempReg)) return false;

	ParsedLine second = parseLine(block.instrs[idx + 1].text);
	if (second.kind != LineKind::Instruction) return false;
	if (second.mnemonic != "mov" || second.operands.size() != 2) return false;
	if (second.operands[1] != tempReg) return false;

	const std::string &dstReg = second.operands[0];
	if (regClass(dstReg) != regClass(tempReg)) return false;

	if (!machineRegDeadAfter(block, idx + 1, tempReg)) return false;

	replaceMachineInstr(block.instrs[idx + 1],
	                    makeMachineInsn("mov", {dstReg, srcReg}));
	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool canPropagateCopy(const ParsedLine &line,
                             const std::string &tempReg,
                             const std::string &srcReg,
                             ParsedLine &rewritten) {
	auto rewriteUses = [&](std::initializer_list<size_t> useIndices) {
		bool replaced = false;
		for (size_t idx : useIndices) {
			if (idx >= rewritten.operands.size()) continue;
			if (rewritten.operands[idx] != tempReg) continue;
			rewritten.operands[idx] = srcReg;
			replaced = true;
		}
		return replaced;
	};

	rewritten = line;
	if (line.mnemonic == "cmp" || line.mnemonic == "cmn" || line.mnemonic == "fcmp" ||
	    line.mnemonic == "tst" || line.mnemonic == "ccmp") {
		return rewriteUses({0, 1});
	}
	return false;
}

static bool tryMachineCopyPropagate(MachineBasicBlock &block, size_t idx) {
	ParsedLine copy = parseLine(block.instrs[idx].text);
	if (copy.kind != LineKind::Instruction) return false;
	if (copy.mnemonic != "mov" && copy.mnemonic != "fmov") return false;
	if (copy.operands.size() != 2) return false;

	const std::string &tempReg = copy.operands[0];
	const std::string &srcReg = copy.operands[1];
	if (tempReg == srcReg || samePhysicalReg(tempReg, srcReg)) return false;
	if (srcReg == "sp" || srcReg == "wzr" || srcReg == "xzr") return false;

	char tempCls = regClass(tempReg);
	char srcCls = regClass(srcReg);
	if (!tempCls || !srcCls || tempCls != srcCls) return false;
	if (copy.mnemonic == "mov" && (tempCls == 's' || tempCls == 'd')) return false;
	if (copy.mnemonic == "fmov" && tempCls != 's' && tempCls != 'd') return false;

	for (size_t i = idx + 1; i < block.instrs.size(); ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind == LineKind::Empty || line.kind == LineKind::Comment)
			continue;
		if (line.kind != LineKind::Instruction)
			return false;
		if (isCallBarrier(line.mnemonic) || isControlFlowBarrier(line.mnemonic))
			return false;

		if (lineWritesReg(line, srcReg))
			return false;
		if (lineUsesReg(line, tempReg) && !lineReadsReg(line, tempReg))
			return false;
		if (!lineReadsReg(line, tempReg)) {
			if (lineWritesReg(line, tempReg))
				return false;
			continue;
		}

		ParsedLine rewritten;
		if (!canPropagateCopy(line, tempReg, srcReg, rewritten))
			return false;
		if (!machineRegDeadAfter(block, i, tempReg))
			return false;

		replaceMachineInstr(block.instrs[i],
		                    makeMachineInsn(rewritten.mnemonic, rewritten.operands));
		return true;
	}

	return false;
}

static bool machineRegDeadAfter(const MachineBasicBlock &block,
                                size_t idx,
                                const std::string &reg) {
	for (size_t i = idx + 1; i < block.instrs.size(); ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind != LineKind::Instruction) continue;
		if (isCallBarrier(line.mnemonic)) return isScratchReg(reg);
		if (lineWritesReg(line, reg)) return true;
		if (lineReadsReg(line, reg)) return false;
		if (line.mnemonic == "b" || line.mnemonic == "ret" ||
		    line.mnemonic == "cbnz" || line.mnemonic == "cbz" ||
		    line.mnemonic == "tbnz" || line.mnemonic == "tbz" ||
		    (line.mnemonic.size() >= 2 && line.mnemonic[0] == 'b' && line.mnemonic[1] == '.'))
			return false;
	}
	return false;
}

static bool tryMachineZeroStore(MachineBasicBlock &block, size_t idx) {
	auto &inst = block.instrs[idx];
	ParsedLine zero = parseLine(inst.text);
	if (zero.kind != LineKind::Instruction) return false;
	if (zero.mnemonic != "movz" && zero.mnemonic != "mov") return false;
	if (zero.operands.empty()) return false;

	std::string zeroedReg = zero.operands[0];
	char cls = regClass(zeroedReg);
	if (cls != 'w' && cls != 'x') return false;

	bool isZeroing = false;
	if (zero.mnemonic == "movz" && zero.operands.size() >= 2) {
		isZeroing = zero.operands[1] == "#0";
	} else if (zero.mnemonic == "mov" && zero.operands.size() >= 2) {
		const std::string &src = zero.operands[1];
		isZeroing = src == "wzr" || src == "xzr" || src == "#0";
	}
	if (!isZeroing) return false;

	std::string architecturalZero = cls == 'w' ? "wzr" : "xzr";
	bool foundStore = false;
	size_t lastTouched = idx;
	int seen = 0;

	for (size_t i = idx + 1; i < block.instrs.size() && seen < 3; ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind != LineKind::Instruction) continue;
		++seen;

		if (isCallBarrier(line.mnemonic)) return false;
		if (lineWritesReg(line, zeroedReg)) return false;

		if (line.mnemonic == "str" && line.operands.size() >= 2 &&
		    line.operands[0] == zeroedReg) {
			line.operands[0] = architecturalZero;
			replaceMachineInstr(block.instrs[i], makeMachineInsn("str", line.operands));
			foundStore = true;
			lastTouched = i;
			continue;
		}

		if (lineUsesReg(line, zeroedReg)) return false;
	}

	if (!foundStore) return false;
	if (!machineRegDeadAfter(block, lastTouched, zeroedReg)) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static std::vector<size_t> machineInstructionWindow(const MachineBasicBlock &block,
                                                    size_t idx,
                                                    int count) {
	std::vector<size_t> window;
	for (size_t i = idx; i < block.instrs.size() && (int)window.size() < count; ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind == LineKind::Instruction) {
			window.push_back(i);
		} else if (line.kind != LineKind::Empty && line.kind != LineKind::Comment) {
			break;
		}
	}
	return window;
}

static bool validAddSubSourceForClass(const std::string &reg, char cls) {
	char sourceCls = regClass(reg);
	if (sourceCls == cls) return true;
	return cls == 'x' && reg == "sp";
}

static bool tryMachineImmediateFold(MachineBasicBlock &block, size_t idx) {
	ParsedLine movz = parseLine(block.instrs[idx].text);
	if (movz.kind != LineKind::Instruction) return false;
	if (movz.mnemonic != "movz") return false;
	if (movz.operands.size() != 2) return false;

	std::string tempReg = movz.operands[0];
	if (!isScratchReg(tempReg)) return false;
	char cls = regClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;

	const std::string &imm = movz.operands[1];
	if (imm.empty() || imm[0] != '#') return false;
	int value = std::atoi(imm.c_str() + 1);
	if (value < 0 || value > kAddSubImm12Max) return false;

	auto window = machineInstructionWindow(block, idx + 1, 2);
	if (window.size() < 2) return false;

	size_t aluIdx = window[0];
	size_t movIdx = window[1];
	ParsedLine alu = parseLine(block.instrs[aluIdx].text);
	ParsedLine outMov = parseLine(block.instrs[movIdx].text);

	if (alu.mnemonic != "add" && alu.mnemonic != "sub") return false;
	if (alu.operands.size() != 3) return false;

	std::string middleReg = alu.operands[0];
	if (!isScratchReg(middleReg)) return false;
	if (regClass(middleReg) != cls) return false;

	std::string sourceReg;
	if (alu.operands[2] == tempReg) {
		sourceReg = alu.operands[1];
	} else if (alu.mnemonic == "add" && alu.operands[1] == tempReg) {
		sourceReg = alu.operands[2];
	} else {
		return false;
	}
	if (sourceReg == tempReg) return false;
	if (!validAddSubSourceForClass(sourceReg, cls)) return false;

	if (outMov.mnemonic != "mov" || outMov.operands.size() != 2) return false;
	if (outMov.operands[1] != middleReg) return false;
	std::string dstReg = outMov.operands[0];
	if (regClass(dstReg) != cls) return false;

	if (!machineRegDeadAfter(block, movIdx, middleReg)) return false;
	if (!machineRegDeadAfter(block, movIdx, tempReg)) return false;

	std::string newImm = "#" + std::to_string(value);
	replaceMachineInstr(block.instrs[movIdx],
	                    makeMachineInsn(alu.mnemonic, {dstReg, sourceReg, newImm}));

	block.instrs.erase(block.instrs.begin() + aluIdx);
	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool tryMachineFoldAddSubMov(MachineBasicBlock &block, size_t idx) {
	ParsedLine alu = parseLine(block.instrs[idx].text);
	if (alu.kind != LineKind::Instruction) return false;
	if (alu.mnemonic != "add" && alu.mnemonic != "sub") return false;
	if (alu.operands.size() != 3) return false;

	const std::string &imm = alu.operands[2];
	if (imm.empty() || imm[0] != '#') return false;

	std::string tempReg = alu.operands[0];
	if (!isScratchReg(tempReg)) return false;
	char cls = regClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (!validAddSubSourceForClass(alu.operands[1], cls)) return false;

	auto window = machineInstructionWindow(block, idx + 1, 6);
	for (size_t movIdx : window) {
		ParsedLine line = parseLine(block.instrs[movIdx].text);
		if (line.kind != LineKind::Instruction) continue;
		if (isCallBarrier(line.mnemonic)) return false;
		if (lineWritesReg(line, tempReg)) return false;

		bool isMov = line.mnemonic == "mov" && line.operands.size() == 2;
		if (lineUsesReg(line, tempReg) && !isMov) return false;
		if (!isMov || line.operands[1] != tempReg)
			continue;

		std::string dstReg = line.operands[0];
		if (regClass(dstReg) != cls) return false;

		if (dstReg == tempReg) {
			block.instrs.erase(block.instrs.begin() + movIdx);
			return true;
		}

		for (size_t j = idx + 1; j < movIdx; ++j) {
			ParsedLine between = parseLine(block.instrs[j].text);
			if (between.kind != LineKind::Instruction) continue;
			if (lineUsesReg(between, dstReg) || lineWritesReg(between, dstReg))
				return false;
		}

		if (!machineRegDeadAfter(block, movIdx, tempReg)) return false;

		std::vector<std::string> newOperands = alu.operands;
		newOperands[0] = dstReg;
		replaceMachineInstr(block.instrs[idx], makeMachineInsn(alu.mnemonic, newOperands));
		block.instrs.erase(block.instrs.begin() + movIdx);
		return true;
	}

	return false;
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
static bool tryMachineDelayAddSubToCopy(
    MachineBasicBlock &block, size_t idx,
    const MachineLivenessResult &liveness) {
	ParsedLine alu = parseLine(block.instrs[idx].text);
	if (alu.kind != LineKind::Instruction) return false;
	if (alu.mnemonic != "add" && alu.mnemonic != "sub") return false;
	if (alu.operands.size() != 3) return false;

	const std::string tempReg = alu.operands[0];
	char cls = regClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (regClass(alu.operands[1]) != cls && alu.operands[1] != "sp")
		return false;
	if (alu.operands[2].empty() || alu.operands[2][0] != '#')
		return false;

	std::set<std::string> sourceRegs = block.instrs[idx].uses;
	bool sawConditionalBranch = false;
	std::string branchTarget;
	int seen = 0;
	for (size_t i = idx + 1; i < block.instrs.size() && seen < 10; ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind == LineKind::Empty || line.kind == LineKind::Comment)
			continue;
		if (line.kind != LineKind::Instruction)
			return false;
		++seen;

		bool isCopy = line.mnemonic == "mov" && line.operands.size() == 2 &&
		              line.operands[1] == tempReg;
		if (isCopy) {
			if (!sawConditionalBranch) return false;
			const std::string &dstReg = line.operands[0];
			if (regClass(dstReg) != cls || dstReg == tempReg) return false;

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

			std::vector<std::string> operands = alu.operands;
			operands[0] = dstReg;
			replaceMachineInstr(block.instrs[i],
			                    makeMachineInsn(alu.mnemonic, operands));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}

		if (lineReadsReg(line, tempReg) || lineWritesReg(line, tempReg))
			return false;
		for (const auto &source : sourceRegs) {
			std::string physical = source;
			if (!physical.empty() && physical[0] == 'r')
				physical = std::string(1, cls) + physical.substr(1);
			if (lineWritesReg(line, physical))
				return false;
		}

		if (isCallBarrier(line.mnemonic) || line.mnemonic == "b" ||
		    line.mnemonic == "ret")
			return false;
		if (isControlFlowBarrier(line.mnemonic)) {
			if (sawConditionalBranch)
				return false;
			if (line.mnemonic.size() < 3 ||
			    line.mnemonic[0] != 'b' || line.mnemonic[1] != '.' ||
			    line.operands.size() != 1)
				return false;
			sawConditionalBranch = true;
			branchTarget = line.operands[0];
		}
	}
	return false;
}

static bool deadAfterConsumer(const MachineBasicBlock &block,
                              size_t consumerIdx,
                              const std::string &removedReg,
                              const std::string &consumerDst) {
	if (samePhysicalReg(removedReg, consumerDst)) return true;
	return machineRegDeadAfter(block, consumerIdx, removedReg);
}

static bool tryMachineMulAddFusion(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine mul = parseLine(block.instrs[idx].text);
	if (mul.kind != LineKind::Instruction) return false;
	if (mul.mnemonic != "mul" || mul.operands.size() != 3) return false;

	const std::string &mulDst = mul.operands[0];
	const std::string &mulOp1 = mul.operands[1];
	const std::string &mulOp2 = mul.operands[2];
	char cls = regClass(mulDst);
	if (cls != 'w' && cls != 'x') return false;
	if (regClass(mulOp1) != cls || regClass(mulOp2) != cls) return false;

	size_t consumerIdx = idx + 1;
	std::string effectiveSrc = mulDst;
	std::string forwardedReg;

	ParsedLine consumer = parseLine(block.instrs[consumerIdx].text);
	if (consumer.kind != LineKind::Instruction) return false;
	if (consumer.mnemonic == "mov" && consumer.operands.size() == 2 &&
	    consumer.operands[1] == mulDst && regClass(consumer.operands[0]) == cls) {
		forwardedReg = consumer.operands[0];
		if (samePhysicalReg(forwardedReg, mulOp1) ||
		    samePhysicalReg(forwardedReg, mulOp2))
			return false;
		if (idx + 2 >= block.instrs.size()) return false;
		consumerIdx = idx + 2;
		effectiveSrc = forwardedReg;
		consumer = parseLine(block.instrs[consumerIdx].text);
		if (consumer.kind != LineKind::Instruction) return false;
	}

	if (consumer.mnemonic != "add" && consumer.mnemonic != "sub") return false;
	if (consumer.operands.size() != 3) return false;

	const std::string &dst = consumer.operands[0];
	const std::string &lhs = consumer.operands[1];
	const std::string &rhs = consumer.operands[2];
	if (regClass(dst) != cls) return false;

	std::string replacementMnemonic;
	std::vector<std::string> replacementOperands;

	if (consumer.mnemonic == "add") {
		std::string acc;
		if (lhs == effectiveSrc && regClass(rhs) == cls) {
			acc = rhs;
		} else if (rhs == effectiveSrc && regClass(lhs) == cls) {
			acc = lhs;
		} else {
			return false;
		}
		if (samePhysicalReg(acc, effectiveSrc)) return false;
		replacementMnemonic = "madd";
		replacementOperands = {dst, mulOp1, mulOp2, acc};
	} else {
		if (rhs != effectiveSrc) return false;
		if (lhs == "wzr" || lhs == "xzr") {
			replacementMnemonic = "mneg";
			replacementOperands = {dst, mulOp1, mulOp2};
		} else if (regClass(lhs) == cls) {
			if (samePhysicalReg(lhs, effectiveSrc)) return false;
			replacementMnemonic = "msub";
			replacementOperands = {dst, mulOp1, mulOp2, lhs};
		} else {
			return false;
		}
	}

	if (!deadAfterConsumer(block, consumerIdx, mulDst, dst)) return false;
	if (!forwardedReg.empty() && !deadAfterConsumer(block, consumerIdx, forwardedReg, dst))
		return false;

	replaceMachineInstr(block.instrs[consumerIdx],
	                    makeMachineInsn(replacementMnemonic, replacementOperands));
	if (!forwardedReg.empty()) {
		block.instrs.erase(block.instrs.begin() + idx + 1);
		block.instrs.erase(block.instrs.begin() + idx);
	} else {
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}

static bool tryMachineStoreLoadForward(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine store = parseLine(block.instrs[idx].text);
	ParsedLine load = parseLine(block.instrs[idx + 1].text);
	if (store.kind != LineKind::Instruction || load.kind != LineKind::Instruction)
		return false;
	if (store.mnemonic != "str" || load.mnemonic != "ldr") return false;
	if (store.operands.size() != 2 || load.operands.size() != 2) return false;

	MemOperand storeMem = parseMemOp(store.operands[1]);
	MemOperand loadMem = parseMemOp(load.operands[1]);
	if (!storeMem.valid || !loadMem.valid) return false;
	if (storeMem.base != "x29" || loadMem.base != "x29") return false;
	if (storeMem.offset != loadMem.offset) return false;

	const std::string &src = store.operands[0];
	const std::string &dst = load.operands[0];
	char cls = regClass(src);
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;
	if (regClass(dst) != cls) return false;

	if (samePhysicalReg(src, dst)) {
		block.instrs.erase(block.instrs.begin() + idx + 1);
		return true;
	}

	std::string movMnemonic = (cls == 's' || cls == 'd') ? "fmov" : "mov";
	replaceMachineInstr(block.instrs[idx + 1],
	                    makeMachineInsn(movMnemonic, {dst, src}));
	return true;
}

static bool isControlFlowBarrier(const std::string &mnemonic) {
	return mnemonic == "b" || mnemonic == "ret" ||
	       mnemonic == "cbnz" || mnemonic == "cbz" ||
	       mnemonic == "tbnz" || mnemonic == "tbz" ||
	       (mnemonic.size() >= 2 && mnemonic[0] == 'b' && mnemonic[1] == '.');
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

// Does this instruction set NZCV flags?
static bool setsFlags(const std::string &mnemonic) {
	if (mnemonic == "cmp" || mnemonic == "tst" ||
	    mnemonic == "fcmps" || mnemonic == "fcmpe" ||
	    mnemonic == "adds" || mnemonic == "subs" ||
	    mnemonic == "ands")
		return true;
	// The `s` suffix on ALU instructions (adds, subs, ands) sets flags.
	if (mnemonic.size() >= 2 && mnemonic.back() == 's')
		return true;
	return false;
}

static bool tryMachineAndTBZ(MachineBasicBlock &block, size_t idx) {
	ParsedLine andLine = parseLine(block.instrs[idx].text);
	if (andLine.kind != LineKind::Instruction) return false;
	if (andLine.mnemonic != "and") return false;
	if (andLine.operands.size() != 3) return false;
	if (andLine.operands[2] != "#1") return false;

	const std::string &tempReg = andLine.operands[0];
	const std::string &srcReg  = andLine.operands[1];
	char cls = regClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (regClass(srcReg) != cls) return false;

	// Scan forward for a consumer of tempReg.
	// We stop at barriers (calls, CF changes, writes to tempReg).
	enum { MaxScan = 8 };
	size_t scanEnd = std::min(idx + MaxScan, block.instrs.size());

	for (size_t i = idx + 1; i < scanEnd; ++i) {
		ParsedLine line = parseLine(block.instrs[i].text);
		if (line.kind != LineKind::Instruction) continue;

		if (isCallBarrier(line.mnemonic)) return false;

		// tempReg must not be redefined before consumption
		if (lineWritesReg(line, tempReg)) return false;

		// ── Direct cbz / cbnz ──────────────────────────────────
		bool isCbz  = (line.mnemonic == "cbz"  && line.operands.size() >= 2 &&
		               line.operands[0] == tempReg);
		bool isCbnz = (line.mnemonic == "cbnz" && line.operands.size() >= 2 &&
		               line.operands[0] == tempReg);

		// Bail on other control-flow barriers (unrelated cbz/cbnz, ret, b, etc.)
		if (!isCbz && !isCbnz && isControlFlowBarrier(line.mnemonic))
			return false;

		if (isCbz || isCbnz) {
			// Make sure tempReg isn't read by any instruction between
			// the and and the cbz/cbnz (other than the cbz/cbnz itself).
			bool usedElsewhere = false;
			for (size_t j = idx + 1; j < i; ++j) {
				ParsedLine between = parseLine(block.instrs[j].text);
				if (between.kind != LineKind::Instruction) continue;
				if (lineReadsReg(between, tempReg)) { usedElsewhere = true; break; }
			}
			if (usedElsewhere) return false;

			const std::string &label = line.operands[1];
			std::string newMnemonic = isCbz ? "tbz" : "tbnz";
			replaceMachineInstr(block.instrs[i],
			                    makeMachineInsn(newMnemonic,
			                                    {srcReg, "#0", label}));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}

		// ── tst wN, wN  or  cmp wN, #0 → b.eq / b.ne ─────────
		bool isTst = (line.mnemonic == "tst" && line.operands.size() >= 2 &&
		              line.operands[0] == tempReg && line.operands[1] == tempReg);
		bool isCmpZero = (line.mnemonic == "cmp" && line.operands.size() >= 2 &&
		                  line.operands[0] == tempReg &&
		                  line.operands[1] == "#0");

		if (!isTst && !isCmpZero) {
			// tempReg is used by some other instruction — bail out
			if (lineUsesReg(line, tempReg)) return false;
			continue;
		}

		// tempReg should not be read by other instructions between and → tst/cmp
		bool usedElsewhere = false;
		for (size_t j = idx + 1; j < i; ++j) {
			ParsedLine between = parseLine(block.instrs[j].text);
			if (between.kind != LineKind::Instruction) continue;
			if (lineReadsReg(between, tempReg)) { usedElsewhere = true; break; }
		}
		if (usedElsewhere) return false;

		// Now look for b.eq / b.ne immediately following (skipping non-instructions)
		size_t branchIdx = i + 1;
		while (branchIdx < block.instrs.size()) {
			ParsedLine brLine = parseLine(block.instrs[branchIdx].text);
			if (brLine.kind == LineKind::Empty || brLine.kind == LineKind::Comment) {
				++branchIdx;
				continue;
			}
			if (brLine.kind != LineKind::Instruction) return false;

			// Flags must not be clobbered between tst/cmp and the branch
			for (size_t k = i + 1; k < branchIdx; ++k) {
				ParsedLine between = parseLine(block.instrs[k].text);
				if (between.kind != LineKind::Instruction) continue;
				if (setsFlags(between.mnemonic)) return false;
				if (between.mnemonic == "b" || between.mnemonic == "bl" ||
				    between.mnemonic == "blr" || between.mnemonic == "ret")
					return false;
			}

			if (brLine.mnemonic == "b.eq" && brLine.operands.size() >= 1) {
				replaceMachineInstr(block.instrs[branchIdx],
				                    makeMachineInsn("tbz",
				                                    {srcReg, "#0", brLine.operands[0]}));
				block.instrs.erase(block.instrs.begin() + i);   // remove tst/cmp
				block.instrs.erase(block.instrs.begin() + idx); // remove and
				return true;
			}
			if (brLine.mnemonic == "b.ne" && brLine.operands.size() >= 1) {
				replaceMachineInstr(block.instrs[branchIdx],
				                    makeMachineInsn("tbnz",
				                                    {srcReg, "#0", brLine.operands[0]}));
				block.instrs.erase(block.instrs.begin() + i);   // remove tst/cmp
				block.instrs.erase(block.instrs.begin() + idx); // remove and
				return true;
			}
			return false;
		}
		return false;
	}
	return false;
}

static bool tryMachineRedundantAdrp(MachineBasicBlock &block, size_t idx) {
	ParsedLine first = parseLine(block.instrs[idx].text);
	if (first.kind != LineKind::Instruction) return false;
	if (first.mnemonic != "adrp" || first.operands.size() < 2) return false;

	const std::string &reg = first.operands[0];
	const std::string &symbol = first.operands[1];

	auto window = machineInstructionWindow(block, idx + 1, 20);
	for (size_t wi : window) {
		ParsedLine line = parseLine(block.instrs[wi].text);
		if (line.kind != LineKind::Instruction) continue;
		if (isCallBarrier(line.mnemonic) || isControlFlowBarrier(line.mnemonic))
			return false;
		if (lineWritesReg(line, reg)) return false;

		if (line.mnemonic == "adrp" && line.operands.size() >= 2 &&
		    line.operands[0] == reg && line.operands[1] == symbol) {
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}
	}

	return false;
}

static bool tryMachineRedundantSubFrame(MachineBasicBlock &block, size_t idx) {
	ParsedLine first = parseLine(block.instrs[idx].text);
	if (first.kind != LineKind::Instruction) return false;
	if (first.mnemonic != "sub" || first.operands.size() != 3) return false;
	if (first.operands[0] != "x17" || first.operands[1] != "x29") return false;
	const std::string &imm = first.operands[2];
	if (imm.empty() || imm[0] != '#') return false;

	auto window = machineInstructionWindow(block, idx + 1, 3);
	for (size_t wi : window) {
		ParsedLine line = parseLine(block.instrs[wi].text);
		if (line.kind != LineKind::Instruction) continue;
		if (isCallBarrier(line.mnemonic) || isControlFlowBarrier(line.mnemonic))
			return false;

		if (line.mnemonic == "sub" && line.operands.size() == 3 &&
		    line.operands[0] == "x17" && line.operands[1] == "x29") {
			if (line.operands[2] != imm) return false;
			block.instrs.erase(block.instrs.begin() + wi);
			return true;
		}

		if (lineWritesReg(line, "x17")) return false;
	}

	return false;
}

static bool validPairOffset(int offset, int stride) {
	if (stride <= 0) return false;
	if (offset % stride != 0) return false;
	int scaled = offset / stride;
	return scaled >= -64 && scaled <= 63;
}

static bool tryMachineDeadStore(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine first = parseLine(block.instrs[idx].text);
	ParsedLine second = parseLine(block.instrs[idx + 1].text);
	if (first.kind != LineKind::Instruction || second.kind != LineKind::Instruction)
		return false;
	if (first.mnemonic != "str" || second.mnemonic != "str") return false;
	if (first.operands.size() != 2 || second.operands.size() != 2) return false;

	MemOperand firstMem = parseMemOp(first.operands[1]);
	MemOperand secondMem = parseMemOp(second.operands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;
	if (firstMem.offset != secondMem.offset) return false;

	char firstClass = regClass(first.operands[0]);
	if (firstClass != 'w' && firstClass != 'x' && firstClass != 's' && firstClass != 'd')
		return false;
	if (regClass(second.operands[0]) != firstClass) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

static bool tryMachineMergeStores(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine first = parseLine(block.instrs[idx].text);
	ParsedLine second = parseLine(block.instrs[idx + 1].text);
	if (first.kind != LineKind::Instruction || second.kind != LineKind::Instruction)
		return false;
	if (first.mnemonic != "str" || second.mnemonic != "str") return false;
	if (first.operands.size() != 2 || second.operands.size() != 2) return false;

	MemOperand firstMem = parseMemOp(first.operands[1]);
	MemOperand secondMem = parseMemOp(second.operands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;

	char cls = regClass(first.operands[0]);
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;
	if (regClass(second.operands[0]) != cls) return false;

	int stride = regSize(cls);
	int diff = secondMem.offset - firstMem.offset;
	if (std::abs(diff) != stride) return false;

	bool firstIsLower = diff > 0;
	int lowerOffset = firstIsLower ? firstMem.offset : secondMem.offset;
	if (!validPairOffset(lowerOffset, stride)) return false;

	std::string lowerReg = firstIsLower ? first.operands[0] : second.operands[0];
	std::string higherReg = firstIsLower ? second.operands[0] : first.operands[0];
	std::string addr = "[x29, #" + std::to_string(lowerOffset) + "]";

	if (firstIsLower) {
		replaceMachineInstr(block.instrs[idx],
		                    makeMachineInsn("stp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx + 1);
	} else {
		replaceMachineInstr(block.instrs[idx + 1],
		                    makeMachineInsn("stp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}

static bool tryMachineMergeLoads(MachineBasicBlock &block, size_t idx) {
	if (idx + 1 >= block.instrs.size()) return false;

	ParsedLine first = parseLine(block.instrs[idx].text);
	ParsedLine second = parseLine(block.instrs[idx + 1].text);
	if (first.kind != LineKind::Instruction || second.kind != LineKind::Instruction)
		return false;
	if (first.mnemonic != "ldr" || second.mnemonic != "ldr") return false;
	if (first.operands.size() != 2 || second.operands.size() != 2) return false;

	MemOperand firstMem = parseMemOp(first.operands[1]);
	MemOperand secondMem = parseMemOp(second.operands[1]);
	if (!firstMem.valid || !secondMem.valid) return false;
	if (firstMem.base != "x29" || secondMem.base != "x29") return false;

	const std::string &firstDst = first.operands[0];
	const std::string &secondDst = second.operands[0];
	char cls = regClass(firstDst);
	if (cls != 'w' && cls != 'x' && cls != 's' && cls != 'd') return false;
	if (regClass(secondDst) != cls) return false;
	if (samePhysicalReg(firstDst, secondDst)) return false;
	if (samePhysicalReg(firstDst, firstMem.base) ||
	    samePhysicalReg(secondDst, secondMem.base)) return false;

	int stride = regSize(cls);
	int diff = secondMem.offset - firstMem.offset;
	if (std::abs(diff) != stride) return false;

	bool firstIsLower = diff > 0;
	int lowerOffset = firstIsLower ? firstMem.offset : secondMem.offset;
	if (!validPairOffset(lowerOffset, stride)) return false;

	std::string lowerReg = firstIsLower ? firstDst : secondDst;
	std::string higherReg = firstIsLower ? secondDst : firstDst;
	std::string addr = "[x29, #" + std::to_string(lowerOffset) + "]";

	if (firstIsLower) {
		replaceMachineInstr(block.instrs[idx],
		                    makeMachineInsn("ldp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx + 1);
	} else {
		replaceMachineInstr(block.instrs[idx + 1],
		                    makeMachineInsn("ldp", {lowerReg, higherReg, addr}));
		block.instrs.erase(block.instrs.begin() + idx);
	}
	return true;
}

static std::string labelName(const MachineInstr &inst) {
	ParsedLine line = parseLine(inst.text);
	if (line.kind != LineKind::Label) return "";
	std::string label = trim(line.raw);
	if (!label.empty() && label.back() == ':')
		label.pop_back();
	return label;
}

static bool nextVisibleIsLabel(const MachineFunction &func,
                               size_t blockIdx,
                               size_t instrIdx,
                               const std::string &target) {
	for (size_t b = blockIdx; b < func.blocks.size(); ++b) {
		const auto &instrs = func.blocks[b].instrs;
		size_t begin = (b == blockIdx) ? instrIdx + 1 : 0;
		for (size_t i = begin; i < instrs.size(); ++i) {
			ParsedLine line = parseLine(instrs[i].text);
			if (line.kind == LineKind::Empty || line.kind == LineKind::Comment)
				continue;
			if (line.kind == LineKind::Label)
				return labelName(instrs[i]) == target;
			return false;
		}
	}
	return false;
}

// 分支指令的跳转目标操作数下标（目标总是最后一个操作数）；非分支返回 -1。
// 注意 "bl" 是调用不是分支。
static int branchTargetOperandIndex(const ParsedLine &line) {
	const std::string &m = line.mnemonic;
	if (m == "b" && line.operands.size() == 1) return 0;
	if (m.size() > 2 && m.compare(0, 2, "b.") == 0 && line.operands.size() == 1)
		return 0;
	if ((m == "cbz" || m == "cbnz") && line.operands.size() == 2) return 1;
	if ((m == "tbz" || m == "tbnz") && line.operands.size() == 3) return 2;
	return -1;
}

// 找到函数内名为 target 的 label 后第一条会被执行的指令
// （跳过空行/注释/连续 label——顺序执行会穿过它们）；找不到或遇到
// directive 等不可分析内容返回 false。
static bool findFirstInstrAfterLabel(const MachineFunction &func,
                                     const std::string &target,
                                     size_t &outBlock, size_t &outInstr) {
	bool seen = false;
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		const auto &instrs = func.blocks[b].instrs;
		for (size_t i = 0; i < instrs.size(); ++i) {
			ParsedLine line = parseLine(instrs[i].text);
			if (!seen) {
				if (line.kind == LineKind::Label && labelName(instrs[i]) == target)
					seen = true;
				continue;
			}
			if (line.kind == LineKind::Empty || line.kind == LineKind::Comment ||
			    line.kind == LineKind::Label)
				continue;
			if (line.kind != LineKind::Instruction)
				return false;
			outBlock = b;
			outInstr = i;
			return true;
		}
	}
	return false;
}

// 沿 forwarder 链（label 处第一条指令是无条件 b）解析出最终跳转目标；
// 遇环返回空串。
static std::string resolveForwardTarget(const MachineFunction &func,
                                        std::string target) {
	std::set<std::string> visited;
	while (visited.insert(target).second) {
		size_t b = 0, i = 0;
		if (!findFirstInstrAfterLabel(func, target, b, i))
			return target;
		ParsedLine line = parseLine(func.blocks[b].instrs[i].text);
		if (line.mnemonic != "b" || line.operands.size() != 1)
			return target;
		target = line.operands[0];
	}
	return "";
}

// 跳转链折叠：分支目标若是只含一条无条件 b 的 forwarder 块，直接改跳最终目标。
//   b.eq .Ledge_1        →  b.eq real_target
//   .Ledge_1: b real_target
static bool tryMachineBranchThreading(MachineFunction &func,
                                      size_t blockIdx,
                                      size_t instrIdx) {
	auto &inst = func.blocks[blockIdx].instrs[instrIdx];
	ParsedLine line = parseLine(inst.text);
	if (line.kind != LineKind::Instruction) return false;
	int ti = branchTargetOperandIndex(line);
	if (ti < 0) return false;
	const std::string target = line.operands[ti];
	std::string finalTarget = resolveForwardTarget(func, target);
	if (finalTarget.empty() || finalTarget == target) return false;

	size_t pos = inst.text.rfind(target);
	if (pos == std::string::npos) return false;
	inst.text = inst.text.substr(0, pos) + finalTarget +
	            inst.text.substr(pos + target.size());
	return true;
}

// 清理跳转链折叠后遗留的不可达 forwarder 残块：
// label 是函数内部标签、未被函数内任何操作数引用、前一条可见指令是
// 无条件 b/ret（无 fallthrough 进入）、其后紧跟一条无条件 b 时，
// 删除该 label 与这条 b。
static bool tryMachineRemoveDeadForwarder(MachineFunction &func,
                                          size_t blockIdx,
                                          size_t instrIdx) {
	auto &block = func.blocks[blockIdx];
	ParsedLine labelLine = parseLine(block.instrs[instrIdx].text);
	if (labelLine.kind != LineKind::Label) return false;
	std::string label = labelName(block.instrs[instrIdx]);
	if (label.empty() || label == func.name) return false;
	// 仅处理本函数生成的内部标签，避免触碰任何可能被外部引用的符号
	if (label.compare(0, 2, ".L") != 0 &&
	    (label.size() <= func.name.size() + 1 ||
	     label.compare(0, func.name.size() + 1, func.name + "_") != 0))
		return false;

	// 函数内任何指令的任何操作数都不得引用该 label
	for (const auto &bb : func.blocks) {
		for (const auto &other : bb.instrs) {
			ParsedLine l = parseLine(other.text);
			if (l.kind != LineKind::Instruction) continue;
			for (const auto &op : l.operands)
				if (op == label) return false;
		}
	}

	// 前一条可见指令必须是无条件 b 或 ret（否则存在 fallthrough 进入）
	{
		bool ok = false;
		for (size_t b = blockIdx + 1; b-- > 0;) {
			const auto &instrs = func.blocks[b].instrs;
			size_t start = (b == blockIdx) ? instrIdx : instrs.size();
			for (size_t i = start; i-- > 0;) {
				ParsedLine l = parseLine(instrs[i].text);
				if (l.kind == LineKind::Empty || l.kind == LineKind::Comment)
					continue;
				if (l.kind == LineKind::Instruction &&
				    ((l.mnemonic == "b" && l.operands.size() == 1) ||
				     l.mnemonic == "ret"))
					ok = true;
				goto prevChecked;
			}
		}
	prevChecked:
		if (!ok) return false;
	}

	// 其后第一条可见行必须就是无条件 b（中间不允许有其他 label）
	size_t bIdx = blockIdx, iIdx = instrIdx + 1;
	for (; bIdx < func.blocks.size(); ++bIdx, iIdx = 0) {
		auto &instrs = func.blocks[bIdx].instrs;
		for (; iIdx < instrs.size(); ++iIdx) {
			ParsedLine l = parseLine(instrs[iIdx].text);
			if (l.kind == LineKind::Empty || l.kind == LineKind::Comment)
				continue;
			if (l.kind == LineKind::Instruction && l.mnemonic == "b" &&
			    l.operands.size() == 1) {
				func.blocks[bIdx].instrs.erase(func.blocks[bIdx].instrs.begin() + iIdx);
				block.instrs.erase(block.instrs.begin() + instrIdx);
				return true;
			}
			return false;
		}
	}
	return false;
}

static bool tryMachineFallthroughBranch(MachineFunction &func,
                                        size_t blockIdx,
                                        size_t instrIdx) {
	auto &block = func.blocks[blockIdx];
	auto &inst = block.instrs[instrIdx];
	ParsedLine line = parseLine(inst.text);
	if (line.kind != LineKind::Instruction) return false;
	if (line.mnemonic != "b" || line.operands.size() != 1) return false;
	if (!nextVisibleIsLabel(func, blockIdx, instrIdx, line.operands[0])) return false;

	block.instrs.erase(block.instrs.begin() + instrIdx);
	return true;
}

void peepholeOptimize(MachineFunction &func) {
	bool changed = true;
	while (changed) {
		changed = false;
		MachineLivenessResult liveness = MachineLiveness().analyze(func);
		for (size_t b = 0; b < func.blocks.size() && !changed; ++b) {
			for (size_t i = 0; i < func.blocks[b].instrs.size(); ++i) {
				if (tryMachineSelfMove(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineSwapMov(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineForwardMov(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineAndTBZ(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineCopyPropagate(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineImmediateFold(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineFoldAddSubMov(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineDelayAddSubToCopy(func.blocks[b], i, liveness)) {
					changed = true;
					break;
				}
				if (tryMachineMulAddFusion(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineStoreLoadForward(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineRedundantAdrp(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineRedundantSubFrame(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineZeroStore(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineDeadStore(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineMergeStores(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineMergeLoads(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineFallthroughBranch(func, b, i)) {
					changed = true;
					break;
				}
				if (tryMachineBranchThreading(func, b, i)) {
					changed = true;
					break;
				}
				if (tryMachineRemoveDeadForwarder(func, b, i)) {
					changed = true;
					break;
				}
			}
		}
	}
}
