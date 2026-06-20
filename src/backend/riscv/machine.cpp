#include "../../include/backend/riscv/machine.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace riscv {

namespace {

std::string trim(const std::string &s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> splitOperands(const std::string &s) {
    std::vector<std::string> result;
    std::string current;
    int parenDepth = 0;
    for (char ch : s) {
        if (ch == '(') ++parenDepth;
        if (ch == ')') --parenDepth;
        if (ch == ',' && parenDepth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!trim(current).empty()) result.push_back(trim(current));
    return result;
}

bool isRegister(const std::string &s) {
    static const std::set<std::string> named = {
        "zero", "ra", "sp", "gp", "tp", "fp",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "ft8", "ft9", "ft10", "ft11",
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7", "fs8", "fs9", "fs10", "fs11",
        "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"};
    if (named.count(s)) return true;
    if (s.size() < 2 || (s[0] != 'x' && s[0] != 'f')) return false;
    return std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isdigit(c); });
}

void addRegister(std::set<std::string> &set, const std::string &operand) {
    std::string op = trim(operand);
    if (isRegister(op)) {
        set.insert(op == "fp" ? "s0" : op);
        return;
    }
    size_t l = op.find('('), r = op.find(')', l == std::string::npos ? 0 : l + 1);
    if (l != std::string::npos && r != std::string::npos)
        addRegister(set, op.substr(l + 1, r - l - 1));
}

bool isOneOf(const std::string &op, std::initializer_list<const char *> names) {
    for (const char *name : names)
        if (op == name) return true;
    return false;
}

void addCallBoundary(MInst &m) {
    // Calls observe the current stack pointer (including stack-passed
    // arguments) and overwrite ra plus all caller-saved registers.
    m.uses.insert("sp");
    for (int i = 0; i < 8; ++i) {
        m.uses.insert("a" + std::to_string(i));
        m.uses.insert("fa" + std::to_string(i));
        m.defs.insert("a" + std::to_string(i));
        m.defs.insert("fa" + std::to_string(i));
    }
    for (int i = 0; i <= 6; ++i) m.defs.insert("t" + std::to_string(i));
    for (int i = 0; i <= 11; ++i) m.defs.insert("ft" + std::to_string(i));
    m.defs.insert("ra");
}

}  // namespace

MInst MInst::inst(std::string text) {
    MInst m;
    m.text = trim(text);
    if (m.text.empty() || m.text[0] == '#') {
        m.isBarrier = true;
        return m;
    }

    size_t split = m.text.find_first_of(" \t");
    m.mnemonic = lower(m.text.substr(0, split));
    if (split != std::string::npos)
        m.operands = splitOperands(m.text.substr(split + 1));

    const std::string &op = m.mnemonic;
    if (isOneOf(op, {"call"})) {
        m.opcode = MOpcode::Call;
        m.isCall = m.isBarrier = true;
        addCallBoundary(m);
    } else if (op == "ret") {
        m.opcode = MOpcode::Ret;
        m.isTerminator = m.isBarrier = true;
        m.uses.insert("a0");
        m.uses.insert("fa0");
        m.uses.insert("sp");
        m.uses.insert("ra");
    } else if (isOneOf(op, {"j", "beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez"})) {
        m.opcode = MOpcode::Branch;
        m.isTerminator = op == "j";
        for (const auto &operand : m.operands) addRegister(m.uses, operand);
    } else if (isOneOf(op, {"lw", "ld", "flw", "fld"})) {
        m.opcode = MOpcode::Load;
        m.mayLoad = true;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        if (m.operands.size() > 1) addRegister(m.uses, m.operands[1]);
    } else if (isOneOf(op, {"sw", "sd", "fsw", "fsd"})) {
        m.opcode = MOpcode::Store;
        m.mayStore = true;
        for (const auto &operand : m.operands) addRegister(m.uses, operand);
    } else if (isOneOf(op, {"mv", "fmv.s", "fmv.d", "fmv.w.x", "fmv.x.w"})) {
        m.opcode = MOpcode::Move;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        for (size_t i = 1; i < m.operands.size(); ++i) addRegister(m.uses, m.operands[i]);
    } else if (isOneOf(op, {"li", "la"})) {
        m.opcode = op == "la" ? MOpcode::Address : MOpcode::Move;
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
    } else {
        m.opcode = op.find("div") != std::string::npos || op.find("rem") != std::string::npos
                       ? MOpcode::Div
                       : (op.find("mul") != std::string::npos ? MOpcode::Mul : MOpcode::Alu);
        if (!m.operands.empty()) addRegister(m.defs, m.operands[0]);
        for (size_t i = 1; i < m.operands.size(); ++i) addRegister(m.uses, m.operands[i]);
        if (m.defs.empty() && m.uses.empty()) {
            m.opcode = MOpcode::Unknown;
            m.isBarrier = true;
        }
    }
    m.defs.erase("zero");
    return m;
}

MInst MInst::label(std::string label) {
    MInst m;
    m.text = std::move(label) + ":";
    m.mnemonic = m.text;
    m.opcode = MOpcode::Label;
    m.isLabel = m.isBarrier = true;
    return m;
}

MInst MInst::directive(std::string directive) {
    MInst m;
    m.text = std::move(directive);
    m.opcode = MOpcode::Directive;
    m.isDirective = m.isBarrier = true;
    return m;
}

std::string printMFunction(const MFunction &func) {
    std::ostringstream os;
    for (const auto &mi : func.insts) {
        if (mi.isLabel || mi.isDirective)
            os << mi.text << "\n";
        else
            os << "\t" << mi.text << "\n";
    }
    return os.str();
}

std::string dumpMFunction(const MFunction &func) {
    std::ostringstream os;
    os << "machine-function " << func.name << "\n";
    for (const auto &mi : func.insts) {
        os << "  " << mi.text << " ; defs={";
        bool first = true;
        for (const auto &reg : mi.defs) {
            if (!first) os << ',';
            os << reg;
            first = false;
        }
        os << "} uses={";
        first = true;
        for (const auto &reg : mi.uses) {
            if (!first) os << ',';
            os << reg;
            first = false;
        }
        os << "}\n";
    }
    return os.str();
}

}  // namespace riscv
