#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cctype>
#include <cstdlib>

std::string peephTrim(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n')) --e;
	return s.substr(b, e - b);
}

char peephRegClass(const std::string &r) {
	if (r.empty()) return 0;
	if (r == "wzr") return 'w';
	if (r == "xzr") return 'x';
	if (r == "sp") return 'x';
	char c = r[0];
	if ((c == 'w' || c == 'x' || c == 's' || c == 'd' || c == 'q' || c == 'v') &&
	    r.size() >= 2 && std::isdigit(r[1]))
		return c;
	return 0;
}

int peephRegSize(char cls) {
	if (cls == 'w' || cls == 's') return 4;
	if (cls == 'x' || cls == 'd') return 8;
	if (cls == 'q' || cls == 'v') return 16;
	return 0;
}

bool peephParsePhysicalReg(const std::string &reg, char &cls, std::string &num) {
	std::string text = peephTrim(reg);
	if (text.size() >= 2 && text.front() == '{' && text.back() == '}')
		text = peephTrim(text.substr(1, text.size() - 2));
	if (text == "wzr" || text == "xzr" || text == "sp")
		return false;
	if (text.size() < 2)
		return false;

	cls = text[0];
	if (cls != 'w' && cls != 'x' && cls != 's' &&
	    cls != 'd' && cls != 'q' && cls != 'v')
		return false;

	size_t pos = 1;
	while (pos < text.size() && std::isdigit(text[pos]))
		++pos;
	if (pos == 1)
		return false;
	num = text.substr(1, pos - 1);
	return true;
}

bool peephSamePhysicalReg(const std::string &a, const std::string &b) {
	if (a == b) return true;
	char aCls = 0, bCls = 0;
	std::string aNum, bNum;
	if (!peephParsePhysicalReg(a, aCls, aNum) || !peephParsePhysicalReg(b, bCls, bNum))
		return false;
	bool sameIntFile = (aCls == 'w' || aCls == 'x') && (bCls == 'w' || bCls == 'x');
	bool sameFloatFile = (aCls == 's' || aCls == 'd' || aCls == 'q' || aCls == 'v') &&
	                     (bCls == 's' || bCls == 'd' || bCls == 'q' || bCls == 'v');
	if (!sameIntFile && !sameFloatFile) return false;
	return aNum == bNum;
}

MemOperand peephParseMemOp(const std::string &s) {
	MemOperand m;
	std::string t = peephTrim(s);
	if (t.empty() || t[0] != '[') return m;
	size_t close = t.find(']');
	if (close == std::string::npos) return m;
	if (close + 1 < t.size()) {
		char after = t[close + 1];
		if (after == '!' || after == ',') return m;
	}
	std::string inner = t.substr(1, close - 1);
	size_t comma = inner.find(',');
	if (comma == std::string::npos) {
		m.base = peephTrim(inner);
		m.offset = 0;
		m.valid = true;
	} else {
		m.base = peephTrim(inner.substr(0, comma));
		std::string offStr = peephTrim(inner.substr(comma + 1));
		if (!offStr.empty() && offStr[0] == '#') {
			m.offset = std::atoi(offStr.c_str() + 1);
			m.valid = true;
		}
	}
	return m;
}

std::string peephMakeInsn(const std::string &mnemonic,
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

void peephReplaceInstr(MachineInstr &inst, const std::string &text) {
	int originalIndex = inst.originalIndex;
	MachineInstr parsed = parseMachineInstr(text, originalIndex);
	parsed.originalIndex = originalIndex;
	inst = std::move(parsed);
}

bool peephIsInertLine(const MachineInstr &inst) {
	return inst.isLabelLike &&
	       inst.opcode != MOpcode::Label &&
	       inst.opcode != MOpcode::Directive;
}

bool peephLineReadsReg(const MachineInstr &l, const std::string &r) {
	if (l.rawOperands.empty()) return false;
	for (size_t i = 1; i < l.rawOperands.size(); ++i)
		if (l.rawOperands[i] == r || peephSamePhysicalReg(l.rawOperands[i], r)) return true;
	if (l.opcodeText == "str" || l.opcodeText == "stp" || l.opcodeText == "stur" ||
	    l.opcodeText == "cbnz" || l.opcodeText == "cbz" ||
	    l.opcodeText == "cmp" || l.opcodeText == "fcmp" || l.opcodeText == "tst")
		if (l.rawOperands[0] == r || peephSamePhysicalReg(l.rawOperands[0], r)) return true;
	return false;
}

bool peephLineUsesReg(const MachineInstr &l, const std::string &r) {
	for (const auto &op : l.rawOperands) {
		if (op == r || peephSamePhysicalReg(op, r)) return true;
		MemOperand mem = peephParseMemOp(op);
		if (mem.valid && peephSamePhysicalReg(mem.base, r)) return true;
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

bool peephLineWritesReg(const MachineInstr &l, const std::string &r) {
	if ((l.opcodeText == "ldr" || l.opcodeText == "str" ||
	     l.opcodeText == "ld1" || l.opcodeText == "st1") &&
	    l.rawOperands.size() >= 3) {
		MemOperand mem = peephParseMemOp(l.rawOperands[1]);
		if (mem.valid && peephSamePhysicalReg(mem.base, r))
			return true;
	}
	if (l.opcodeText == "str" || l.opcodeText == "stp" ||
	    l.opcodeText == "cmp" || l.opcodeText == "fcmp" ||
	    l.opcodeText == "cbnz" || l.opcodeText == "cbz" ||
	    l.opcodeText == "b" || l.opcodeText == "bl" || l.opcodeText == "blr" ||
	    l.opcodeText == "ret" || l.opcodeText == "st1")
		return false;
	if (l.opcodeText == "ldp") {
		for (int i = 0; i < 2 && i < (int)l.rawOperands.size(); ++i) {
			const std::string &op = l.rawOperands[i];
			if (op == r) return true;
			if (op.size() >= 2 && r.size() >= 2 &&
			    ((op[0] == 'w' || op[0] == 'x') && (r[0] == 'w' || r[0] == 'x')) &&
			    op.substr(1) == r.substr(1)) return true;
		}
		return false;
	}
	if (!l.rawOperands.empty()) {
		const std::string &dst = l.rawOperands[0];
		if (dst == r) return true;
		if (peephSamePhysicalReg(dst, r)) return true;
	}
	return false;
}

bool peephRegDeadAfter(const MachineBasicBlock &block, size_t idx,
                       const std::string &reg,
                       const MachineLivenessResult &liveness) {
	if (idx >= block.instrs.size()) return false;
	auto liveIt = liveness.instrLiveOut.find(&block.instrs[idx]);
	if (liveIt == liveness.instrLiveOut.end()) return false;

	char cls = 0;
	std::string number;
	std::string normalized = reg;
	if (peephParsePhysicalReg(reg, cls, number)) {
		if (cls == 'w' || cls == 'x')
			normalized = "r" + number;
		else
			normalized = "v" + number;
	}
	return liveIt->second.count(normalized) == 0;
}

std::vector<size_t> peephInstrWindow(const MachineBasicBlock &block,
                                     size_t idx, int count) {
	std::vector<size_t> window;
	for (size_t i = idx; i < block.instrs.size() && (int)window.size() < count; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (!line.isLabelLike) {
			window.push_back(i);
		} else if (!peephIsInertLine(line)) {
			break;
		}
	}
	return window;
}

bool peephIsControlFlowBarrier(const MachineInstr &inst) {
	const std::string &mnemonic = inst.opcodeText;
	return mnemonic == "b" || mnemonic == "ret" ||
	       mnemonic == "cbnz" || mnemonic == "cbz" ||
	       mnemonic == "tbnz" || mnemonic == "tbz" ||
	       (mnemonic.size() >= 2 && mnemonic[0] == 'b' && mnemonic[1] == '.');
}
