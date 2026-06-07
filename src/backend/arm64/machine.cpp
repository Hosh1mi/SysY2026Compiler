#include "../../include/backend/arm64/machine.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

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

bool startsWith(const std::string &s, const std::string &prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string normalizeReg(std::string reg) {
    reg = lower(reg);
    if (reg == "wzr" || reg == "xzr")
        return "zr";
    if (reg == "sp")
        return "sp";
    if (reg.size() < 2)
        return reg;

    char kind = reg[0];
    std::string num = reg.substr(1);
    if (kind == 'w' || kind == 'x')
        return "r" + num;
    if (kind == 's' || kind == 'd' || kind == 'q' || kind == 'v')
        return "v" + num;
    return reg;
}

std::vector<std::string> extractRegs(const std::string &s) {
    static const std::regex regRe("\\b(?:[wxsdqv][0-9]+|wzr|xzr|sp)\\b");
    std::vector<std::string> regs;
    auto begin = std::sregex_iterator(s.begin(), s.end(), regRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
        regs.push_back(normalizeReg((*it).str()));
    return regs;
}

std::vector<std::string> splitOperands(const std::string &operandText) {
    std::vector<std::string> operands;
    std::string cur;
    int bracketDepth = 0;
    for (char c : operandText) {
        if (c == '[') bracketDepth++;
        if (c == ']') bracketDepth--;
        if (c == ',' && bracketDepth == 0) {
            operands.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!trim(cur).empty())
        operands.push_back(trim(cur));
    return operands;
}

void addUses(MachineInstr &mi, const std::string &text) {
    for (const auto &reg : extractRegs(text)) {
        if (reg != "zr")
            mi.uses.insert(reg);
    }
}

void addDef(MachineInstr &mi, const std::string &text) {
    auto regs = extractRegs(text);
    if (regs.empty())
        return;
    if (regs[0] != "zr")
        mi.defs.insert(regs[0]);
}

bool touchesStackPointer(const MachineInstr &mi) {
    return mi.defs.count("sp") || mi.uses.count("sp");
}

bool touchesLinkOrFrameCritical(const MachineInstr &mi) {
    return mi.defs.count("r30") || mi.uses.count("r30");
}

} // namespace

MachineInstr parseMachineInstr(const std::string &line, int originalIndex) {
    MachineInstr mi;
    mi.text = line;
    mi.originalIndex = originalIndex;

    std::string t = trim(line);
    if (t.empty()) {
        mi.isBarrier = true;
        mi.isLabelLike = true;
        return mi;
    }

    if (t.back() == ':') {
        mi.opcode = MOpcode::Label;
        mi.isBarrier = true;
        mi.isLabelLike = true;
        mi.opcodeText = "label";
        return mi;
    }

    if (startsWith(t, ".")) {
        mi.opcode = MOpcode::Directive;
        mi.isBarrier = true;
        mi.isLabelLike = true;
        mi.opcodeText = "directive";
        return mi;
    }

    if (startsWith(t, "//")) {
        mi.opcode = MOpcode::Comment;
        mi.isBarrier = true;
        mi.isLabelLike = true;
        mi.opcodeText = "comment";
        return mi;
    }

    std::string op;
    std::string rest;
    {
        std::istringstream iss(t);
        iss >> op;
        std::getline(iss, rest);
    }
    op = lower(op);
    rest = trim(rest);
    mi.opcodeText = op;
    auto operands = splitOperands(rest);

    auto defFirstUseRest = [&]() {
        if (!operands.empty())
            addDef(mi, operands[0]);
        for (size_t i = 1; i < operands.size(); ++i)
            addUses(mi, operands[i]);
    };

    if (op == "ldr" || op == "ldur" || op == "ld1") {
        mi.opcode = MOpcode::Load;
        mi.mayLoad = true;
        mi.latency = 4;
        defFirstUseRest();
    } else if (op == "ldp") {
        mi.opcode = MOpcode::PairLoad;
        mi.mayLoad = true;
        mi.latency = 4;
        if (!operands.empty()) addDef(mi, operands[0]);
        if (operands.size() > 1) addDef(mi, operands[1]);
        for (size_t i = 2; i < operands.size(); ++i) addUses(mi, operands[i]);
    } else if (op == "str" || op == "stur" || op == "st1") {
        mi.opcode = MOpcode::Store;
        mi.mayStore = true;
        mi.isBarrier = true;
        addUses(mi, rest);
    } else if (op == "stp") {
        mi.opcode = MOpcode::PairStore;
        mi.mayStore = true;
        mi.isBarrier = true;
        addUses(mi, rest);
    } else if (op == "cmp" || op == "cmn" || op == "fcmp") {
        mi.opcode = MOpcode::Cmp;
        mi.setsFlags = true;
        mi.isBarrier = true;
        addUses(mi, rest);
    } else if (op == "cset" || op == "csel" || op == "ccmp") {
        mi.opcode = MOpcode::FlagUse;
        mi.usesFlags = true;
        mi.isBarrier = true;
        defFirstUseRest();
    } else if (op == "b" || startsWith(op, "b.") || op == "cbz" || op == "cbnz") {
        mi.opcode = MOpcode::Branch;
        mi.isBarrier = true;
        if (startsWith(op, "b."))
            mi.usesFlags = true;
        addUses(mi, rest);
    } else if (op == "bl") {
        mi.opcode = MOpcode::Call;
        mi.isBarrier = true;
    } else if (op == "ret") {
        mi.opcode = MOpcode::Ret;
        mi.isBarrier = true;
    } else if (op == "adrp" || op == "adr") {
        mi.opcode = MOpcode::Adr;
        mi.latency = 1;
        if (!operands.empty())
            addDef(mi, operands[0]);
    } else if (op == "movz") {
        mi.opcode = MOpcode::Mov;
        if (!operands.empty())
            addDef(mi, operands[0]);
    } else if (op == "movk") {
        mi.opcode = MOpcode::Mov;
        if (!operands.empty()) {
            addDef(mi, operands[0]);
            addUses(mi, operands[0]);
        }
    } else if (op == "mov" || op == "fmov" || op == "mvn" || op == "sxtw") {
        mi.opcode = MOpcode::Mov;
        defFirstUseRest();
    } else if (op == "mul" || op == "madd" || op == "msub" || op == "mneg" ||
               op == "fmul") {
        mi.opcode = MOpcode::Mul;
        mi.latency = (op == "fmul") ? 5 : 3;
        defFirstUseRest();
    } else if (op == "sdiv" || op == "udiv" || op == "fdiv") {
        mi.opcode = MOpcode::Div;
        mi.latency = 12;
        defFirstUseRest();
    } else if (op == "add" || op == "sub" || op == "and" || op == "orr" ||
               op == "eor" || op == "bic" || op == "asr" || op == "lsl" ||
               op == "lsr" || op == "fadd" || op == "fsub" || op == "fneg") {
        mi.opcode = MOpcode::Alu;
        mi.latency = (op == "fadd" || op == "fsub" || op == "fneg") ? 4 : 1;
        defFirstUseRest();
    } else {
        mi.opcode = MOpcode::Unknown;
        defFirstUseRest();
    }

    if (touchesStackPointer(mi) || touchesLinkOrFrameCritical(mi))
        mi.isBarrier = true;

    return mi;
}
