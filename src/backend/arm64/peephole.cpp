#include "../../include/backend/arm64/peephole.hpp"
#include <cctype>
#include <cstdlib>
#include <map>
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
    // For most instructions, operands[0] is destination, rest are sources
    for (size_t i = 1; i < l.operands.size(); ++i)
        if (l.operands[i] == r) return true;
    // Stores, compares, branches: operands[0] is also a source
    if (l.mnemonic == "str" || l.mnemonic == "stp" || l.mnemonic == "stur" ||
        l.mnemonic == "cbnz" || l.mnemonic == "cbz" ||
        l.mnemonic == "cmp" || l.mnemonic == "fcmp" || l.mnemonic == "tst")
        if (l.operands[0] == r) return true;
    return false;
}

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

// Scratch register range — must agree with the AArch64 codegen's caller-saved
// scratch allocator (backend/arm64/load_store.cpp).  w10–w15 / x10–x15 are the
// short-lived temporaries the emitter uses for intermediate values within a
// single materialization (e.g. the movz→add→mov triple that tryImmediateFold
// folds).  If the allocator's scratch range changes, both sides must move.
static constexpr int kScratchRegMin = 10;
static constexpr int kScratchRegMax = 15;

// AArch64 add/sub unshifted imm12 range.  The `lsl #12` variant ([4096,
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

// Liveness check: starting after `startIdx`, decide whether `r` is dead — i.e.
// safe to delete the producing instruction.  Three outcomes scanning forward:
//   - call barrier → dead (calls clobber caller-saved scratch regs)
//   - write to r before any read → dead (overwritten before being observed)
//   - read of r before any write → live (cannot delete the producer)
// Returns true on dead, false on live OR on falling off the end of the region
// (conservative: no proof of death == live).
//
// Limitation: does not follow control flow.  A subsequent branch joining a
// region where r is still live is invisible here.  For the patterns that
// invoke this — scratch regs used within a single emitter materialization —
// the next instruction is always within the same basic block, so the linear
// scan is sufficient in practice.
static bool regDeadAfter(const std::vector<ParsedLine> &lines, size_t startIdx,
                         const std::string &r) {
    for (size_t j = startIdx + 1; j < lines.size(); ++j) {
        if (lines[j].kind != LineKind::Instruction) continue;
        if (isCallBarrier(lines[j].mnemonic)) return true;
        if (lineWritesReg(lines[j], r))       return true;
        if (lineReadsReg(lines[j], r))        return false;
    }
    return false;
}

// Rule 0a: swap-mov elimination
//   mov rA, rB  +  mov rB, rA  →  keep first, delete second
//   After `mov rA, rB`, rA = old_B.  `mov rB, rA` then sets rB = old_B,
//   which rB already held — the second instruction is a no-op.
static bool trySwapMov(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "mov") return false;
    if (l0.operands.size() < 2) return false;

    std::string rA = l0.operands[0];
    std::string rB = l0.operands[1];
    if (rA == rB) return false;
    if (rB.empty() || rB[0] == '#') return false;  // rB must be a register
    if (!regClass(rA) || !regClass(rB)) return false;

    auto w = instructionWindow(lines, idx + 1, 1);
    if (w.empty()) return false;
    auto &l1 = lines[w[0]];

    if (l1.mnemonic != "mov" || l1.operands.size() < 2) return false;
    if (l1.operands[0] != rB || l1.operands[1] != rA) return false;

    l1.raw.clear();
    l1.kind = LineKind::Empty;
    return true;
}

// Rule 0b: immediate fold.
//   movz wT, #N  +  add/sub wM, wS, wT  +  mov wD, wM  →  add/sub wD, wS, #N
//   Also handles the commutative-add form: movz wT, #N + add wM, wT, wS + mov wD, wM.
//
// Constraints:
//   - N ∈ [0, kAddSubImm12Max] (4095) — bare imm12 only; lsl #12 variant skipped.
//   - wT, wM are scratch regs (kScratchRegMin..kScratchRegMax) — matches the
//     emitter's short-lived-temporary contract.
//   - l1 must have exactly 3 operands (no `, lsl #k` / `, sxtw` etc.):
//     `add Rd, X, Y, lsl #2` lowered from `x*5` was once folded to `add Rd, X, #5`,
//     silently dropping the lsl — that historic bug motivates the strict check.
//   - wS != wT: `add wM, wT, wT` (= 2N) folded to `add wD, wT, #N` would read
//     an uninitialized wT after the movz is deleted.
//   - Both wT and wM must be dead after l2 (regDeadAfter): nothing later in
//     the same straight-line region observes their pre-fold values.
static bool tryImmediateFold(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "movz") return false;
    if (l0.operands.size() < 2) return false;

    std::string wT = l0.operands[0];
    if (!isScratchReg(wT)) return false;
    char cls = regClass(wT);
    if (cls != 'w' && cls != 'x') return false;

    const std::string &immStr = l0.operands[1];
    if (immStr.empty() || immStr[0] != '#') return false;
    int N = std::atoi(immStr.c_str() + 1);
    if (N < 0 || N > kAddSubImm12Max) return false;

    auto w = instructionWindow(lines, idx + 1, 2);
    if (w.size() < 2) return false;

    auto &l1 = lines[w[0]];
    auto &l2 = lines[w[1]];

    if (l1.mnemonic != "add" && l1.mnemonic != "sub") return false;
    if (l1.operands.size() != 3) return false;  // no shift/extend modifier — see header

    std::string wM = l1.operands[0];
    if (!isScratchReg(wM)) return false;
    if (regClass(wM) != cls) return false;

    std::string wS;
    if (l1.operands[2] == wT)                                    wS = l1.operands[1];
    else if (l1.mnemonic == "add" && l1.operands[1] == wT)       wS = l1.operands[2];
    else                                                          return false;
    if (wS == wT) return false;

    if (l2.mnemonic != "mov" || l2.operands.size() < 2) return false;
    if (l2.operands[1] != wM) return false;
    std::string wD = l2.operands[0];

    // Both scratch regs must be dead after the triple — otherwise the deletion
    // of movz/op leaves a later read with a stale or undefined value.  wT was
    // historically not checked here; that was relying on an undocumented
    // emitter contract.  Now made explicit.
    if (!regDeadAfter(lines, w[1], wM)) return false;
    if (!regDeadAfter(lines, w[1], wT)) return false;

    std::string newImm = "#" + std::to_string(N);
    l0.raw.clear(); l0.kind = LineKind::Empty;
    l1.raw.clear(); l1.kind = LineKind::Empty;
    l2.raw = makeInsn(l1.mnemonic, {wD, wS, newImm});
    l2.mnemonic = l1.mnemonic;
    l2.operands = {wD, wS, newImm};
    return true;
}

// Rule 1: forward mov  —  mov rA, rB  +  mov rC, rA  →  mov rC, rB
static bool tryForwardMov(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "mov") return false;
    if (l0.operands.size() < 2) return false;

    std::string rA = l0.operands[0];
    std::string rB = l0.operands[1];
    if (rA == rB) return false;

    auto w = instructionWindow(lines, idx + 1, 1);
    if (w.empty()) return false;
    auto &l1 = lines[w[0]];
    if (l1.mnemonic != "mov") return false;
    if (l1.operands.size() < 2) return false;
    if (l1.operands[1] != rA) return false;

    // rA must not be read after the second mov — scan until rA is overwritten.
    int seen = 0;
    bool safe = false;
    for (size_t j = w[0] + 1; j < lines.size() && seen < 30; ++j) {
        if (lines[j].kind != LineKind::Instruction) continue;
        ++seen;

        // Stop at control-flow barriers — we can't track liveness across BBs.
        const std::string &mnem = lines[j].mnemonic;
        if (mnem == "b" || mnem == "ret" ||
            mnem == "cbnz" || mnem == "cbz" ||
            mnem == "tbnz" || mnem == "tbz" ||
            (!mnem.empty() && mnem[0] == 'b' && mnem[1] == '.'))  // b.cond
            return false;

        if (lineReadsReg(lines[j], rA)) return false;  // rA read → unsafe
        if (lineWritesReg(lines[j], rA)) { safe = true; break; }
    }
    if (!safe) return false;  // rA not overwritten within window → conservative

    // Transform: mov rC, rA  →  mov rC, rB
    l1.operands[1] = rB;
    l1.raw = makeInsn("mov", l1.operands);

    // Delete first mov
    l0.raw.clear();
    l0.kind = LineKind::Empty;
    return true;
}

// Rule 2: delete mov r, r  and  fmov r, r
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

// Rule 3: movz wN, #0  +  str wN, [...]  →  str wzr, [...]
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

// Rule 4: store-load forwarding (x29 base only)
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

// Rule 5: adjacent store merging → stp  (x29 base only)
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

// Rule 6: adjacent load merging → ldp  (x29 base only)
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

// Rule 7: dead store elimination (x29 base only)
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

// Rule 8: eliminate redundant sub x17, x29, #N
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
//   sub x17, x29, #N  then  str rA, [x17]  then later sub x17, x29, #N  ldr rB, [x17]
//   →  keep sub+str, replace the later sub+ldr with mov rB, rA.
// x17 may be temporarily reused (sub to a different offset) between the
// store and the load, as long as the original slot [x29-#N] isn't overwritten.
static bool tryStoreLoadForwardX17(std::vector<ParsedLine> &lines, size_t idx) {
	// Look for: sub x17, x29, #N at idx, then str rA, [x17] at idx+1
	auto &l0 = lines[idx];
	if (l0.mnemonic != "sub") return false;
	if (l0.operands.size() < 3) return false;
	if (l0.operands[0] != "x17" || l0.operands[1] != "x29") return false;

	// Extract the immediate offset from the first sub
	const std::string &immStr0 = l0.operands[2];
	if (immStr0.empty() || immStr0[0] != '#') return false;
	int origImm = std::atoi(immStr0.c_str() + 1);

	auto w = instructionWindow(lines, idx + 1, 20);
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

	// Track x17: which slot it points to, whether orig slot was overwritten
	int curImm = origImm;
	bool origOverwritten = false;

	for (size_t j = 1; j < w.size(); ++j) {
		auto &li = lines[w[j]];
		if (isCallBarrier(li.mnemonic)) return false;
		if (lineWritesReg(li, srcReg)) return false;

		// sub x17, x29, #M: x17 now points to slot M
		if (li.mnemonic == "sub" && li.operands.size() >= 3 &&
		    li.operands[0] == "x17" && li.operands[1] == "x29") {
			const std::string &imm2 = li.operands[2];
			if (!imm2.empty() && imm2[0] == '#')
				curImm = std::atoi(imm2.c_str() + 1);
			continue;
		}
		// Any non-sub write to x17 loses our address
		if (lineWritesReg(li, "x17")) return false;

		// str/stp via x17 overwrites the CURRENT slot
		if (li.mnemonic == "str" || li.mnemonic == "stp") {
			int opIdx = (li.mnemonic == "str" && li.operands.size() >= 2) ? 1 : -1;
			if (li.mnemonic == "stp" && li.operands.size() >= 3) opIdx = 2;
			if (opIdx >= 0) {
				auto mi = parseMemOp(li.operands[opIdx]);
				if (mi.valid && mi.base == "x17" && curImm == origImm)
					origOverwritten = true;
			}
			continue;
		}

		// ldr via x17: load from current slot
		if (li.mnemonic != "ldr") continue;
		if (li.operands.size() < 2) continue;
		auto mLdr = parseMemOp(li.operands[1]);
		if (!mLdr.valid || mLdr.base != "x17") continue;
		if (curImm != origImm || origOverwritten) continue;

		std::string dstReg = li.operands[0];
		if (regClass(dstReg) != cls) return false;

		// Found! Replace the preceding sub + this ldr with mov.
		if (dstReg != srcReg) {
			std::string movOp = (cls == 's') ? "fmov" : "mov";
			li.raw = makeInsn(movOp, {dstReg, srcReg});
			li.mnemonic = movOp;
			li.operands = {dstReg, srcReg};
		} else {
			li.raw.clear();
			li.kind = LineKind::Empty;
		}
		
		// Delete the sub that set up this ldr, but only if it
		// actually is a sub (Rule 7 may have already deleted it).
		auto &prevLine = lines[w[j-1]];
		if (prevLine.mnemonic == "sub" && prevLine.operands.size() >= 3 &&
		    prevLine.operands[0] == "x17" && prevLine.operands[1] == "x29") {
			prevLine.raw.clear();
			prevLine.kind = LineKind::Empty;
		}

		return true;
	}
	return false;
}

// Rule 9: mul + add/sub fusion → madd / msub / mneg
//   mul wA, wB, wC  then  add wD, wA, wE  →  madd wD, wB, wC, wE
//   mul wA, wB, wC  then  sub wD, wE, wA  →  msub wD, wB, wC, wE
//   mul wA, wB, wC  then  sub wD, wzr, wA  →  mneg wD, wB, wC
// Also handles a single mov forwarding the mul result:
//   mul wA, wB, wC  then  mov wD, wA  then  add wE, wD, wF  →  madd wE, wB, wC, wF
static bool tryMulAddFusion(std::vector<ParsedLine> &lines, size_t idx) {
    auto &lMul = lines[idx];
    if (lMul.mnemonic != "mul" || lMul.operands.size() < 3) return false;

    std::string mulDst = lMul.operands[0];   // wA
    std::string mulOp1 = lMul.operands[1];   // wB
    std::string mulOp2 = lMul.operands[2];   // wC
    char cls = regClass(mulDst);
    if (cls != 'w' && cls != 'x') return false;
    if (regClass(mulOp1) != cls || regClass(mulOp2) != cls) return false;

    std::string fwdDst;   // forwarded register (if mov interposes)
    size_t fwdIdx = 0;     // index of the mov line to clear later

    auto w = instructionWindow(lines, idx + 1, 6);
    for (size_t wi : w) {
        auto &li = lines[wi];

        // Bail if mul result or forwarded reg is clobbered
        if (lineWritesReg(li, mulDst)) return false;
        if (!fwdDst.empty() && lineWritesReg(li, fwdDst)) return false;
        if (lineWritesReg(li, mulOp1)) return false;
        if (lineWritesReg(li, mulOp2)) return false;
        if (isCallBarrier(li.mnemonic)) return false;

        // --- mov forwarding: mov wD, wA where wA == mulDst ---
        if (li.mnemonic == "mov" && li.operands.size() >= 2) {
            std::string movDst = li.operands[0];
            std::string movSrc = li.operands[1];
            if (fwdDst.empty() && movSrc == mulDst && regClass(movDst) == cls) {
                // Track the forwarded register
                fwdDst = movDst;
                fwdIdx = wi;
                continue;
            }
            // Another mov — not our forwarded register, bail if it uses mulDst
            if (lineUsesReg(li, mulDst)) return false;
            if (!fwdDst.empty() && lineUsesReg(li, fwdDst)) return false;
            continue;
        }

        // --- effective source register for the add/sub ---
        std::string effectiveSrc = fwdDst.empty() ? mulDst : fwdDst;

        // Check if effectiveSrc is used by this non-add/sub instruction
        if (li.mnemonic != "add" && li.mnemonic != "sub") {
            if (lineUsesReg(li, mulDst)) return false;
            if (!fwdDst.empty() && lineUsesReg(li, fwdDst)) return false;
            continue;
        }
        if (li.operands.size() < 3) continue;

        std::string addDst = li.operands[0];
        std::string addOp1 = li.operands[1];
        std::string addOp2 = li.operands[2];
        if (regClass(addDst) != cls) continue;

        // ---- madd ----
        if (li.mnemonic == "add") {
            if ((addOp1 == effectiveSrc && regClass(addOp2) == cls) ||
                (addOp2 == effectiveSrc && regClass(addOp1) == cls)) {
                std::string acc = (addOp1 == effectiveSrc) ? addOp2 : addOp1;
                lMul.raw.clear(); lMul.kind = LineKind::Empty;
                if (!fwdDst.empty()) {lines[fwdIdx].raw.clear(); lines[fwdIdx].kind = LineKind::Empty;}
                li.raw = makeInsn("madd", {addDst, mulOp1, mulOp2, acc});
                li.mnemonic = "madd";
                li.operands = {addDst, mulOp1, mulOp2, acc};
                return true;
            }
            return false;
        }

        // ---- msub / mneg ----
        if (li.mnemonic == "sub") {
            if (addOp2 == effectiveSrc && regClass(addOp1) == cls) {
                lMul.raw.clear(); lMul.kind = LineKind::Empty;
                if (!fwdDst.empty()) {lines[fwdIdx].raw.clear(); lines[fwdIdx].kind = LineKind::Empty;}
                li.raw = makeInsn("msub", {addDst, mulOp1, mulOp2, addOp1});
                li.mnemonic = "msub";
                li.operands = {addDst, mulOp1, mulOp2, addOp1};
                return true;
            }
            if (addOp2 == effectiveSrc && (addOp1 == "wzr" || addOp1 == "xzr")) {
                lMul.raw.clear(); lMul.kind = LineKind::Empty;
                if (!fwdDst.empty()) {lines[fwdIdx].raw.clear(); lines[fwdIdx].kind = LineKind::Empty;}
                li.raw = makeInsn("mneg", {addDst, mulOp1, mulOp2});
                li.mnemonic = "mneg";
                li.operands = {addDst, mulOp1, mulOp2};
                return true;
            }
            return false;
        }
    }
    return false;
}

// Rule 10: sxtw CSE within basic blocks.
//   Repeated sxtw xA, wB (same source wB) in the same basic block:
//   cache (i64)wB in a free x-register after the first sxtw, then
//   replace later sxtw with mov xA, xCache.
static const std::vector<std::string> sxtwCacheCandidates = {
    "x11", "x12", "x13", "x14", "x15"
};

static bool trySxtwCSE(std::vector<ParsedLine> &lines) {
    bool changed = false;

    // Find basic blocks: sequences of instructions between labels
    for (size_t bbStart = 0; bbStart < lines.size(); ) {
        // Skip non-instruction lines at the start
        while (bbStart < lines.size() && lines[bbStart].kind != LineKind::Instruction)
            ++bbStart;
        if (bbStart >= lines.size()) break;

        // Find the end of this basic block (next label or directive)
        size_t bbEnd = bbStart;
        while (bbEnd < lines.size() && lines[bbEnd].kind == LineKind::Instruction)
            ++bbEnd;

        // Collect all sxtw instructions in this block, grouped by source register
        // Map: source w-register → vector of (line index, dest x-register)
        struct SxtwInfo { size_t idx; std::string dstReg; };
        std::map<std::string, std::vector<SxtwInfo>> sxtwGroups;

        for (size_t i = bbStart; i < bbEnd; ++i) {
            auto &l = lines[i];
            if (l.kind != LineKind::Instruction) continue;
            if (l.mnemonic != "sxtw" || l.operands.size() < 2) continue;
            std::string dst = l.operands[0];
            std::string src = l.operands[1];
            if (regClass(dst) != 'x') continue;
            if (regClass(src) != 'w') continue;
            sxtwGroups[src].push_back({i, dst});
        }

        // Process each group with ≥3 occurrences (need at least 3 to amortize
        // the cache-setup mov)
        for (auto &[srcReg, group] : sxtwGroups) {
            if (group.size() < 3) continue;

            // Find a free cache register: x11-x15 not written anywhere
            // from the basic-block start to the last sxtw of this group.
            // Must scan from bbStart (not group.front().idx) because a
            // previous peephole pass may have inserted a cache mov for
            // a different source register before this group's first sxtw.
            std::set<std::string> writtenRegs;
            for (size_t i = bbStart; i <= group.back().idx; ++i) {
                auto &l = lines[i];
                if (l.kind != LineKind::Instruction) continue;
                for (const auto &cand : sxtwCacheCandidates)
                    if (lineWritesReg(l, cand))
                        writtenRegs.insert(cand);
                // Also track w-variant writes (wN write zero-extends to xN)
                for (const auto &cand : sxtwCacheCandidates)
                    if (!cand.empty() && cand[0] == 'x') {
                        std::string wVer = "w" + cand.substr(1);
                        if (lineWritesReg(l, wVer))
                            writtenRegs.insert(cand);
                    }
            }
            std::string cacheReg;
            for (const auto &cand : sxtwCacheCandidates) {
                if (!writtenRegs.count(cand) && cand != group.front().dstReg) {
                    cacheReg = cand; break;
                }
            }
            if (cacheReg.empty()) continue;

            // Also verify srcReg is not modified between first and last sxtw
            bool srcModified = false;
            for (size_t i = group.front().idx + 1; i < group.back().idx; ++i) {
                if (lines[i].kind != LineKind::Instruction) continue;
                if (lineWritesReg(lines[i], srcReg)) { srcModified = true; break; }
                // Calls clobber caller-saved registers
                if (isCallBarrier(lines[i].mnemonic)) { srcModified = true; break; }
            }
            if (srcModified) continue;

            // --- Apply the optimization ---
            // 1. After the first sxtw, insert: mov cacheReg, dstReg
            auto &first = lines[group.front().idx];
            std::string firstDst = first.operands[0];
            // Change first sxtw to also write cacheReg (can't; sxtw only has one dst).
            // Instead, emit sxtw as-is, then add mov cacheReg, firstDst after it.
            ParsedLine cacheLine;
            cacheLine.kind = LineKind::Instruction;
            cacheLine.mnemonic = "mov";
            cacheLine.operands = {cacheReg, firstDst};
            cacheLine.raw = makeInsn("mov", {cacheReg, firstDst});
            lines.insert(lines.begin() + group.front().idx + 1, cacheLine);

            // Adjust all subsequent indices (they shifted by 1)
            for (auto &info : group)
                if (info.idx > group.front().idx) ++info.idx;
            ++bbEnd; // block end also shifted

            // 2. Replace subsequent sxtw with mov dstReg, cacheReg
            for (size_t gi = 1; gi < group.size(); ++gi) {
                auto &l = lines[group[gi].idx];
                std::string dstReg = l.operands[0];
                l.mnemonic = "mov";
                l.operands = {dstReg, cacheReg};
                l.raw = makeInsn("mov", {dstReg, cacheReg});
            }

            changed = true;
            break; // one group per block per pass to keep indices stable
        }

        bbStart = bbEnd;
    }

    return changed;
}

// Rule 11: fold add/sub + mov → add/sub with forwarded destination
//   add rX, rY, #imm  +  mov rZ, rX  →  add rZ, rY, #imm
//   sub rX, rY, #imm  +  mov rZ, rX  →  sub rZ, rY, #imm
// When rX is only consumed by the mov and dead after it.
// Handles intervening instructions that don't touch rX (e.g., two independent
// add→mov pairs interleaved).
static bool tryFoldAddSubMov(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "add" && l0.mnemonic != "sub") return false;
    if (l0.operands.size() < 3) return false;

    // Third operand must be an immediate (#0..#4095)
    const std::string &immOp = l0.operands[2];
    if (immOp.empty() || immOp[0] != '#') return false;

    std::string rX = l0.operands[0];  // add/sub destination
    std::string rY = l0.operands[1];  // add/sub first source
    char cls = regClass(rX);
    if (cls != 'w' && cls != 'x') return false;
    if (rX == "wzr" || rX == "xzr") return false;
    if (!isScratchReg(rX)) return false;

    auto w = instructionWindow(lines, idx + 1, 6);

    for (size_t wi : w) {
        auto &li = lines[wi];

        // Calls clobber caller-saved registers — rX may be clobbered
        if (isCallBarrier(li.mnemonic)) return false;

        // If rX is written before the mov, it's a different value now
        if (lineWritesReg(li, rX)) return false;

        // If rX is read by a non-mov instruction, the add has another consumer
        bool isMov = (li.mnemonic == "mov" && li.operands.size() >= 2);
        if (lineUsesReg(li, rX) && !isMov) return false;

        // Match: mov rZ, rX
        if (isMov && li.operands[1] == rX) {
            std::string rZ = li.operands[0];
            if (regClass(rZ) != cls) return false;

            // Self-mov: just delete it (keep add unchanged)
            if (rZ == rX) {
                li.raw.clear();
                li.kind = LineKind::Empty;
                return true;
            }

            // Safety check: rX must not be read after the mov, since the
            // transformation stops updating rX.  Scan forward until rX is
            // overwritten or a control-flow barrier is reached.
            int seen = 0;
            bool safe = false;
            for (size_t j = wi + 1; j < lines.size() && seen < 30; ++j) {
                if (lines[j].kind != LineKind::Instruction) continue;
                ++seen;

                const std::string &mnem = lines[j].mnemonic;
                if (mnem == "b" || mnem == "ret" ||
                    mnem == "cbnz" || mnem == "cbz" ||
                    mnem == "tbnz" || mnem == "tbz" ||
                    (!mnem.empty() && mnem[0] == 'b' && mnem[1] == '.')) {
                    return false;
                }
                if (lineReadsReg(lines[j], rX)) return false;
                if (lineWritesReg(lines[j], rX)) { safe = true; break; }
            }
            if (!safe) return false;

            // Transform: add/sub rZ, rY, #imm
            l0.operands[0] = rZ;
            l0.raw = makeInsn(l0.mnemonic, l0.operands);

            // Delete the mov
            li.raw.clear();
            li.kind = LineKind::Empty;
            return true;
        }
    }
    return false;
}

// Rule 12: eliminate redundant adrp to the same symbol.
//   adrp xN, sym  ... (no write to xN) ...  adrp xN, sym  →  delete second
static bool tryRedundantAdrp(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "adrp" || l0.operands.size() < 2) return false;
    std::string reg = l0.operands[0];
    std::string sym = l0.operands[1];

    auto w = instructionWindow(lines, idx + 1, 20);
    for (size_t wi : w) {
        auto &li = lines[wi];
        if (isCallBarrier(li.mnemonic)) return false; // call clobbers caller-saved xN
        if (lineWritesReg(li, reg)) return false;     // reg overwritten
        if (li.mnemonic == "adrp" && li.operands.size() >= 2 &&
            li.operands[0] == reg && li.operands[1] == sym) {
            li.raw.clear();
            li.kind = LineKind::Empty;
            return true;
        }
    }
    return false;
}

// Rule 13: eliminate redundant unconditional branch to immediately
// following label.  After block reordering this catches the remaining
// cases that the layout pass could not handle (e.g. multi-predecessor
// blocks where only one predecessor can get fallthrough).
//
//   b .Lfoo          →  (deleted)
//   .Lfoo:
static bool tryFallthroughBranch(std::vector<ParsedLine> &lines, size_t idx) {
    auto &l0 = lines[idx];
    if (l0.mnemonic != "b") return false;
    if (l0.operands.size() != 1) return false;

    const std::string &target = l0.operands[0];

    // Find the next non-empty, non-comment line
    for (size_t j = idx + 1; j < lines.size(); ++j) {
        if (lines[j].kind == LineKind::Empty ||
            lines[j].kind == LineKind::Comment)
            continue;
        // Label whose text (minus trailing colon) matches the branch target
        if (lines[j].kind == LineKind::Label) {
            std::string labelText = trim(lines[j].raw);
            // Remove trailing colon
            if (!labelText.empty() && labelText.back() == ':')
                labelText.pop_back();
            if (labelText == target) {
                l0.raw.clear();
                l0.kind = LineKind::Empty;
                return true;
            }
        }
        // Any other kind (instruction, directive) breaks — the branch is
        // not jumping to the immediate next line.
        break;
    }
    return false;
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
				if (tryMachineImmediateFold(func.blocks[b], i)) {
					changed = true;
					break;
				}
				if (tryMachineFoldAddSubMov(func.blocks[b], i)) {
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
			}
		}
	}
}

std::string peepholeOptimize(const std::string &asmText) {
	return asmText;
}
