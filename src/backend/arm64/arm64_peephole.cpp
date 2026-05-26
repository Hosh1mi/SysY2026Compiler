#include "../../include/backend/arm64/arm64_peephole.hpp"
#include <cctype>
#include <cstdlib>
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

static std::vector<ParsedLine> parseLines(const std::string &text) {
	std::vector<ParsedLine> out;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) {
		out.push_back(parseLine(text.substr(pos)));
		break;
		}
		out.push_back(parseLine(text.substr(pos, nl - pos + 1))); // keep newline
		pos = nl + 1;
	}
	return out;
}

static std::string reassemble(const std::vector<ParsedLine> &lines) {
	std::string out;
	for (const auto &l : lines) {
		if (l.kind == LineKind::Empty && l.raw.empty()) continue;
		out += l.raw;
	}
	return out;
}

// ── instruction builder helpers ─────────────────────────────────────

static std::string makeInsn(const std::string &mnemonic,
                            const std::vector<std::string> &operands) {
	std::string s = "\t" + mnemonic;
	if (!operands.empty()) {
		s += "\t";
		for (size_t i = 0; i < operands.size(); ++i) {
		if (i > 0) s += ", ";
		s += operands[i];
		}
	}
	s += "\n";
	return s;
}

// ── helper: count occurrences of a register in a line's operands ────

static bool lineUsesReg(const ParsedLine &l, const std::string &r) {
	for (const auto &op : l.operands) {
		// Check as whole word match
		if (op == r) return true;
		// Check if r appears inside a memory operand
		size_t p = op.find(r);
		if (p != std::string::npos) {
		// Check word boundary before
		if (p > 0 && (std::isalnum(op[p - 1]) || op[p - 1] == '_')) continue;
		// Check word boundary after
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
		return (l.operands.size() >= 2 &&
		        (l.operands[0] == r || l.operands[1] == r));
	}
	if (!l.operands.empty() && l.operands[0] == r) return true;
	return false;
}

// Call instructions (bl, blr) clobber all caller-saved registers.
// Since scratch regs (w9-w15, x9-x15, s16-s31) are all caller-saved,
// any call between a store and a load makes forwarding unsafe.
static bool isCallBarrier(const std::string &m) {
	return m == "bl" || m == "blr";
}

// ── find next N instruction lines starting at idx ──────────────────
// Returns indices into lines of up to count consecutive instructions.
static std::vector<size_t> instructionWindow(const std::vector<ParsedLine> &lines,
                                             size_t idx, int count) {
	std::vector<size_t> w;
	for (size_t i = idx; i < lines.size() && (int)w.size() < count; ++i) {
		if (lines[i].kind == LineKind::Instruction)
		w.push_back(i);
		else if (lines[i].kind != LineKind::Empty && lines[i].kind != LineKind::Comment)
		break; // label or directive breaks the window
	}
	return w;
}

// ── RULE implementations ────────────────────────────────────────────
// Each returns true if it made a change.

// Rule 1: delete mov r, r  and  fmov r, r
static bool trySelfMove(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l = lines[idx];
	if (l.mnemonic != "mov" && l.mnemonic != "fmov") return false;
	if (l.operands.size() < 2) return false;
	if (l.operands[0] == l.operands[1]) {
		l.raw.clear();
		l.kind = LineKind::Empty;
		return true;
	}
	return false;
}

// Rule 2: movz wN, #0  +  str wN, [...]  →  str wzr, [...]
//         mov  wN, wzr +  str wN, [...]  →  str wzr, [...]
//         mov  wN, #0  +  str wN, [...]  →  str wzr, [...]
static bool tryZeroStore(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "movz" && l0.mnemonic != "mov") return false;
	if (l0.operands.empty()) return false;

	std::string zr = l0.operands[0]; // the zeroed register
	char cls = regClass(zr);
	if (cls != 'w' && cls != 'x') return false;

	bool isZeroing = false;
	if (l0.mnemonic == "movz" && l0.operands.size() >= 2) {
		const std::string &imm = l0.operands[1];
		isZeroing = (imm == "#0");
	} else if (l0.mnemonic == "mov" && l0.operands.size() >= 2) {
		const std::string &src = l0.operands[1];
		isZeroing = (src == "wzr" || src == "xzr" || src == "#0");
	}
	if (!isZeroing) return false;

	auto w = instructionWindow(lines, idx + 1, 3);
	if (w.empty()) return false;

	std::string zeroReg = (cls == 'w') ? "wzr" : "xzr";
	bool foundStore = false;
	for (size_t wi : w) {
		auto &li = lines[wi];
		// Calls clobber the zeroed register, can't safely rewrite uses after a call
		if (isCallBarrier(li.mnemonic)) return false;
		if (lineWritesReg(li, zr)) return false; // zr is overwritten, unsafe
		if (li.mnemonic != "str") {
		if (lineUsesReg(li, zr)) return false; // zr used in non-str context
		continue;
		}
		if (li.operands.empty() || li.operands[0] != zr) continue; // not storing zr
		li.operands[0] = zeroReg;
		li.raw = makeInsn("str", li.operands);
		foundStore = true;
	}

	if (!foundStore) return false;

	l0.raw.clear();
	l0.kind = LineKind::Empty;
	return true;
}

// Rule 3: store-load forwarding (x29 base only)
//   str rA, [x29, #OFF]  then  ldr rB, [x29, #OFF]  →  keep str, mov rB, rA
static bool tryStoreLoadForward(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "str") return false;
	if (l0.operands.size() < 2) return false;

	auto m0 = parseMemOp(l0.operands[1]);
	if (!m0.valid || m0.base != "x29") return false;

	std::string srcReg = l0.operands[0];
	char cls = regClass(srcReg);
	if (cls != 'w' && cls != 'x' && cls != 's') return false;
	if (srcReg == "wzr" || srcReg == "xzr") return false; // don't forward zero

	auto w = instructionWindow(lines, idx + 1, 3);
	for (size_t wi : w) {
		auto &li = lines[wi];
		if (li.mnemonic != "ldr") {
		// Calls clobber all caller-saved regs; scratch regs are caller-saved
		if (isCallBarrier(li.mnemonic)) return false;
		// If this instruction writes the source register, forwarding is unsafe
		if (lineWritesReg(li, srcReg)) return false;
		// If the same memory location is overwritten, don't forward
		if (li.mnemonic == "str" && li.operands.size() >= 2) {
			auto mi = parseMemOp(li.operands[1]);
			if (mi.valid && mi.base == "x29" && mi.offset == m0.offset) return false;
		}
		// stp stores to two consecutive words; check overlap with our address
		if (li.mnemonic == "stp" && li.operands.size() >= 3) {
			auto mi = parseMemOp(li.operands[2]);
			if (mi.valid && mi.base == "x29") {
			int stride = regSize(regClass(li.operands[0]));
			if (m0.offset >= mi.offset && m0.offset < mi.offset + 2 * stride)
				return false;
			}
		}
		continue;
		}
		// An ldr that writes srcReg makes forwarding unsafe
		if (lineWritesReg(li, srcReg)) return false;
		if (li.operands.size() < 2) continue;
		auto m1 = parseMemOp(li.operands[1]);
		if (!m1.valid || m1.base != "x29" || m1.offset != m0.offset) continue;

		std::string dstReg = li.operands[0];
		// Must be same register class
		if (regClass(dstReg) != cls) return false;

		if (dstReg == srcReg) {
		// Same register: the ldr is dead (value already in the register)
		li.raw.clear();
		li.kind = LineKind::Empty;
		} else {
		// Replace ldr with mov / fmov
		std::string movOp = (cls == 's') ? "fmov" : "mov";
		li.raw = makeInsn(movOp, {dstReg, srcReg});
		li.mnemonic = movOp;
		li.operands = {dstReg, srcReg};
		}
		return true;
	}
	return false;
}

// Rule 4: adjacent store merging → stp  (x29 base only)
static bool tryMergeStores(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "str") return false;
	if (l0.operands.size() < 2) return false;
	auto m0 = parseMemOp(l0.operands[1]);
	if (!m0.valid || m0.base != "x29") return false;
	char cls0 = regClass(l0.operands[0]);
	if (cls0 != 'w' && cls0 != 'x' && cls0 != 's' && cls0 != 'd') return false;

	auto w = instructionWindow(lines, idx + 1, 3);
	for (size_t wi : w) {
		auto &li = lines[wi];
		if (li.mnemonic != "str" || li.operands.size() < 2) break;
		auto m1 = parseMemOp(li.operands[1]);
		if (!m1.valid || m1.base != "x29") break;
		char cls1 = regClass(li.operands[0]);
		if (cls1 != cls0) break;

		int stride = regSize(cls0); // 4 or 8
		int diff = m1.offset - m0.offset;
		if (std::abs(diff) != stride) break;

		// Determine which store is at the lower address
		ParsedLine *lower, *higher;
		if (diff > 0) { lower = &l0; higher = &li; }
		else          { lower = &li; higher = &l0; }

		int lowerOff = (diff > 0) ? m0.offset : m1.offset;
		// Check stp immediate range
		int range = stride * 63; // imm7 max = 63 (scaled)
		if (lowerOff < -stride * 64 || lowerOff > range) break;
		// Alignment: lower offset must be stride-aligned
		if (lowerOff % stride != 0) break;

		std::string lowerReg = lower->operands[0];
		std::string higherReg = higher->operands[0];
		std::string addr = "[" + m0.base + ", #" + std::to_string(lowerOff) + "]";

		lower->raw = makeInsn("stp", {lowerReg, higherReg, addr});
		lower->mnemonic = "stp";
		lower->operands = {lowerReg, higherReg, addr};
		higher->raw.clear();
		higher->kind = LineKind::Empty;
		return true;
	}
	return false;
}

// Rule 5: adjacent load merging → ldp  (x29 base only)
static bool tryMergeLoads(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "ldr") return false;
	if (l0.operands.size() < 2) return false;
	auto m0 = parseMemOp(l0.operands[1]);
	if (!m0.valid || m0.base != "x29") return false;
	char cls0 = regClass(l0.operands[0]);
	if (cls0 != 'w' && cls0 != 'x' && cls0 != 's' && cls0 != 'd') return false;

	auto w = instructionWindow(lines, idx + 1, 3);
	for (size_t wi : w) {
		auto &li = lines[wi];
		if (li.mnemonic != "ldr" || li.operands.size() < 2) break;
		auto m1 = parseMemOp(li.operands[1]);
		if (!m1.valid || m1.base != "x29") break;
		char cls1 = regClass(li.operands[0]);
		if (cls1 != cls0) break;

		// Don't merge if dest registers overlap
		if (l0.operands[0] == li.operands[0]) break;

		int stride = regSize(cls0);
		int diff = m1.offset - m0.offset;
		if (std::abs(diff) != stride) break;

		// Determine which load is from the lower address
		size_t lowerIdx, higherIdx;
		int lowerOff;
		if (diff > 0) {
		lowerIdx = idx; higherIdx = wi; lowerOff = m0.offset;
		} else {
		lowerIdx = wi; higherIdx = idx; lowerOff = m1.offset;
		}

		int range = stride * 63;
		if (lowerOff < -stride * 64 || lowerOff > range) break;
		if (lowerOff % stride != 0) break;

		std::string lowerReg = lines[lowerIdx].operands[0];
		std::string higherReg = lines[higherIdx].operands[0];
		std::string addr = "[" + m0.base + ", #" + std::to_string(lowerOff) + "]";

		lines[lowerIdx].raw = makeInsn("ldp", {lowerReg, higherReg, addr});
		lines[lowerIdx].mnemonic = "ldp";
		lines[lowerIdx].operands = {lowerReg, higherReg, addr};
		lines[higherIdx].raw.clear();
		lines[higherIdx].kind = LineKind::Empty;
		return true;
	}
	return false;
}

// Rule 6: dead store elimination (x29 base only)
//   str rA, [x29, #OFF]  then  str rB, [x29, #OFF]  →  delete first
static bool tryDeadStore(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "str") return false;
	if (l0.operands.size() < 2) return false;
	auto m0 = parseMemOp(l0.operands[1]);
	if (!m0.valid || m0.base != "x29") return false;

	auto w = instructionWindow(lines, idx + 1, 3);
	for (size_t wi : w) {
		auto &li = lines[wi];
		if (li.mnemonic != "str" || li.operands.size() < 2) {
		// If there's a load from the same address before the overwriting store,
		// the first store is NOT dead (its value is read).
		if (li.mnemonic == "ldr" && li.operands.size() >= 2) {
			auto mi = parseMemOp(li.operands[1]);
			if (mi.valid && mi.base == "x29" && mi.offset == m0.offset) return false;
		}
		continue;
		}
		auto m1 = parseMemOp(li.operands[1]);
		if (m1.valid && m1.base == "x29" && m1.offset == m0.offset) {
		// Same address, same register class
		if (regClass(li.operands[0]) == regClass(l0.operands[0])) {
			l0.raw.clear();
			l0.kind = LineKind::Empty;
			return true;
		}
		}
		// If a different store to same address but different width, conservatively bail
		if (m1.valid && m1.base == "x29" && m1.offset == m0.offset) return false;
	}
	return false;
}

// Rule 7: eliminate redundant sub x17, x29, #N
//   sub x17, x29, #N  ... (no write to x17) ...  sub x17, x29, #N  →  delete second
static bool tryRedundantSub(std::vector<ParsedLine> &lines, size_t idx) {
	auto &l0 = lines[idx];
	if (l0.mnemonic != "sub") return false;
	if (l0.operands.size() < 3) return false;
	// Must be: sub x17, x29, #N
	if (l0.operands[0] != "x17" || l0.operands[1] != "x29") return false;
	const std::string &imm = l0.operands[2];
	if (imm.empty() || imm[0] != '#') return false;

	auto w = instructionWindow(lines, idx + 1, 3);
	for (size_t wi : w) {
		auto &li = lines[wi];
		// If x17 is written by any non-sub instruction, stop tracking
		if (li.mnemonic != "sub") {
		if (lineWritesReg(li, "x17")) return false;
		// Calls may clobber x17
		if (isCallBarrier(li.mnemonic)) return false;
		continue;
		}
		// It's a sub — check if it's the same sub x17, x29, #N
		if (li.operands.size() >= 3 &&
			li.operands[0] == "x17" && li.operands[1] == "x29" &&
			li.operands[2] == imm) {
		li.raw.clear();
		li.kind = LineKind::Empty;
		return true;
		}
		// Different sub (writes x17 to a different value) — stop
		return false;
	}
	return false;
}

// Rule 8: store-load forwarding through x17 (after sub x17, x29, #N)
//   sub x17, x29, #N  then  str rA, [x17]  then  (no write x17)  ldr rB, [x17]
//   →  keep sub+str,  change ldr to mov rB, rA
static bool tryStoreLoadForwardX17(std::vector<ParsedLine> &lines, size_t idx) {
	// Look for: sub x17, x29, #N at idx, then str rA, [x17] at idx+1
	auto &l0 = lines[idx];
	if (l0.mnemonic != "sub") return false;
	if (l0.operands.size() < 3) return false;
	if (l0.operands[0] != "x17" || l0.operands[1] != "x29") return false;

	auto w = instructionWindow(lines, idx + 1, 4); // 4-insn window for sub+str+...+ldr
	if (w.size() < 2) return false;

	// First instruction after sub must be str rA, [x17]
	auto &lStr = lines[w[0]];
	if (lStr.mnemonic != "str" || lStr.operands.size() < 2) return false;
	auto mStr = parseMemOp(lStr.operands[1]);
	if (!mStr.valid || mStr.base != "x17") return false;

	std::string srcReg = lStr.operands[0];
	char cls = regClass(srcReg);
	if (cls != 'w' && cls != 'x' && cls != 's') return false;
	if (srcReg == "wzr" || srcReg == "xzr") return false;

	// Scan remaining window for ldr rB, [x17]
	for (size_t j = 1; j < w.size(); ++j) {
		auto &li = lines[w[j]];
		if (li.mnemonic != "ldr") {
		if (isCallBarrier(li.mnemonic)) return false;
		if (lineWritesReg(li, "x17")) return false;
		if (lineWritesReg(li, srcReg)) return false;
		if (li.mnemonic == "str" && li.operands.size() >= 2) {
			auto mi = parseMemOp(li.operands[1]);
			if (mi.valid && mi.base == "x17") return false; // str via x17 overwrites
		}
		continue;
		}
		if (li.operands.size() < 2) continue;
		auto mLdr = parseMemOp(li.operands[1]);
		if (!mLdr.valid || mLdr.base != "x17") continue;

		std::string dstReg = li.operands[0];
		if (regClass(dstReg) != cls) return false;

		if (dstReg == srcReg) {
		li.raw.clear();
		li.kind = LineKind::Empty;
		} else {
		std::string movOp = (cls == 's') ? "fmov" : "mov";
		li.raw = makeInsn(movOp, {dstReg, srcReg});
		li.mnemonic = movOp;
		li.operands = {dstReg, srcReg};
		}
		return true;
	}
	return false;
}

// ── Main optimization loop ──────────────────────────────────────────

std::string peepholeOptimize(const std::string &asmText) {
	if (asmText.empty()) return asmText;

	std::string current = asmText;
	const int MAX_ITERS = 30;

	for (int iter = 0; iter < MAX_ITERS; ++iter) {
		auto lines = parseLines(current);
		bool changed = false;

		for (size_t i = 0; i < lines.size(); ++i) {
			if (lines[i].kind != LineKind::Instruction) continue;

			if (trySelfMove(lines, i))           { changed = true; break; }
			if (tryZeroStore(lines, i))          { changed = true; break; }
			if (tryStoreLoadForward(lines, i))   { changed = true; break; }
			if (tryRedundantSub(lines, i))       { changed = true; break; }
			if (tryDeadStore(lines, i))          { changed = true; break; }
			if (tryStoreLoadForwardX17(lines, i)){ changed = true; break; }
			if (tryMergeStores(lines, i))        { changed = true; break; }
			if (tryMergeLoads(lines, i))         { changed = true; break; }
		}

		if (!changed) return current;
		current = reassemble(lines);
	}

	return current;
}
