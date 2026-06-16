#include "../../include/backend/arm64/machine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <utility>

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

std::string mnemonicFromText(const std::string &line) {
    std::string t = trim(line);
    if (t.empty())
        return "";
    std::istringstream iss(t);
    std::string op;
    iss >> op;
    return lower(op);
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

int registerWidthBytes(const std::string &operand) {
    static const std::regex regRe("\\b(?:[wxsdqv][0-9]+|wzr|xzr)\\b");
    std::smatch match;
    if (!std::regex_search(operand, match, regRe))
        return 0;
    std::string reg = lower(match.str());
    if (reg.empty())
        return 0;
    switch (reg[0]) {
        case 'w':
        case 's':
            return 4;
        case 'x':
        case 'd':
            return 8;
        case 'q':
            return 16;
        case 'v':
            if (operand.find(".4s") != std::string::npos ||
                operand.find(".16b") != std::string::npos ||
                operand.find(".2d") != std::string::npos)
                return 16;
            return 16;
        default:
            return 0;
    }
}

int memoryWidthBytes(const std::string &op,
                     const std::vector<std::string> &operands) {
    if (op == "ld1" || op == "st1")
        return 16;

    if (op == "ldp" || op == "stp") {
        int w0 = operands.size() > 0 ? registerWidthBytes(operands[0]) : 0;
        int w1 = operands.size() > 1 ? registerWidthBytes(operands[1]) : 0;
        if (w0 > 0 && w1 > 0)
            return w0 + w1;
        return 16;
    }

    if (!operands.empty()) {
        int w = registerWidthBytes(operands[0]);
        if (w > 0)
            return w;
    }
    return 0;
}

bool parseImmediate(const std::string &text, int &value) {
    std::string s = trim(text);
    if (s.empty())
        return false;
    if (s[0] == '#')
        s = s.substr(1);
    if (!s.empty() && s.back() == '!')
        s.pop_back();
    if (s.empty())
        return false;
    char *end = nullptr;
    long parsed = std::strtol(s.c_str(), &end, 0);
    if (!end || *end != '\0')
        return false;
    value = static_cast<int>(parsed);
    return true;
}

void annotateMemoryInfo(MachineInstr &mi, const std::vector<std::string> &operands,
                        const std::string &op) {
    if (!mi.mayLoad && !mi.mayStore)
        return;

    std::string memOperand;
    for (const auto &operand : operands) {
        if (operand.find('[') != std::string::npos) {
            memOperand = operand;
            break;
        }
    }
    if (memOperand.empty())
        return;

    size_t lbr = memOperand.find('[');
    size_t rbr = memOperand.find(']', lbr);
    if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr + 1)
        return;

    std::string inside = memOperand.substr(lbr + 1, rbr - lbr - 1);
    auto addrParts = splitOperands(inside);
    if (addrParts.empty())
        return;

    auto regs = extractRegs(addrParts[0]);
    if (regs.empty())
        return;

    std::string base = regs[0];
    if (base != "r29" && base != "sp")
        return;

    int offset = 0;
    if (addrParts.size() > 1) {
        if (!parseImmediate(addrParts[1], offset))
            return;
    }

    int width = memoryWidthBytes(op, operands);
    if (width <= 0)
        return;

    mi.memBase = base;
    mi.memOffsetKnown = true;
    mi.memOffset = offset;
    mi.memWidth = width;
}

void annotateMemoryInfo(MachineInstr &mi) {
    std::string t = trim(mi.text);
    if (t.empty())
        return;
    std::string op;
    std::string rest;
    {
        std::istringstream iss(t);
        iss >> op;
        std::getline(iss, rest);
    }
    op = lower(op);
    annotateMemoryInfo(mi, splitOperands(trim(rest)), op);
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

MachineInstr MachineInstr::raw(const std::string &line) {
    return parseMachineInstr(line, 0);
}

MachineInstr MachineInstr::make(const std::string &line, MOpcode opcode,
                                std::initializer_list<std::string> defs,
                                std::initializer_list<std::string> uses,
                                int latency) {
    MachineInstr mi;
    mi.text = line;
    mi.opcodeText = mnemonicFromText(line);
    mi.opcode = opcode;
    mi.latency = latency;
    if (opcode == MOpcode::Load || opcode == MOpcode::PairLoad)
        mi.mayLoad = true;
    if (opcode == MOpcode::Store || opcode == MOpcode::PairStore)
        mi.mayStore = true;

    for (const auto &reg : defs) {
        std::string normalized = normalizeReg(reg);
        if (normalized != "zr")
            mi.defs.insert(normalized);
    }
    for (const auto &reg : uses) {
        std::string normalized = normalizeReg(reg);
        if (normalized != "zr")
            mi.uses.insert(normalized);
    }

    annotateMemoryInfo(mi);
    return mi;
}

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
        addUses(mi, rest);
    } else if (op == "stp") {
        mi.opcode = MOpcode::PairStore;
        mi.mayStore = true;
        addUses(mi, rest);
    } else if (op == "cmp" || op == "cmn" || op == "fcmp" || op == "tst") {
        mi.opcode = MOpcode::Cmp;
        mi.setsFlags = true;
        addUses(mi, rest);
    } else if (op == "ccmp") {
        mi.opcode = MOpcode::FlagUse;
        mi.setsFlags = true;
        mi.usesFlags = true;
        addUses(mi, rest);
    } else if (op == "cset" || op == "csel" || op == "cneg" || op == "fcsel") {
        mi.opcode = MOpcode::FlagUse;
        mi.usesFlags = true;
        defFirstUseRest();
    } else if (op == "b" || startsWith(op, "b.") || op == "cbz" || op == "cbnz" ||
               op == "tbz" || op == "tbnz") {
        mi.opcode = MOpcode::Branch;
        mi.isBarrier = true;
        if (startsWith(op, "b."))
            mi.usesFlags = true;
        if (op == "tbz" || op == "tbnz") {
            if (!operands.empty())
                addUses(mi, operands[0]);
        } else {
            addUses(mi, rest);
        }
    } else if (op == "bl") {
        mi.opcode = MOpcode::Call;
        mi.isCall = true;
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
               op == "smull" || op == "umull" || op == "smaddl" || op == "umaddl" ||
               op == "smsubl" || op == "umsubl" || op == "fmul") {
        mi.opcode = MOpcode::Mul;
        bool isVector = rest.find(".4s") != std::string::npos ||
                        rest.find(".2d") != std::string::npos;
        mi.latency = (op == "fmul") ? (isVector ? 4 : 5) : 3;
        defFirstUseRest();
    } else if (op == "mla" || op == "mls") {
        mi.opcode = MOpcode::Neon;
        mi.latency = 3;
        if (!operands.empty()) {
            addDef(mi, operands[0]);
            addUses(mi, operands[0]);
        }
        for (size_t i = 1; i < operands.size(); ++i)
            addUses(mi, operands[i]);
    } else if (op == "addv") {
        mi.opcode = MOpcode::Neon;
        mi.latency = 4;
        defFirstUseRest();
    } else if (op == "movi" || op == "dup") {
        mi.opcode = MOpcode::Neon;
        mi.latency = 1;
        defFirstUseRest();
    } else if (op == "sdiv" || op == "udiv" || op == "fdiv") {
        mi.opcode = MOpcode::Div;
        mi.latency = 12;
        defFirstUseRest();
    } else if (op == "add" || op == "sub" || op == "and" || op == "orr" ||
               op == "eor" || op == "bic" || op == "asr" || op == "lsl" ||
               op == "lsr" || op == "neg" || op == "fadd" || op == "fsub" ||
               op == "fneg" || op == "scvtf" || op == "fcvtzs" || op == "clz") {
        mi.opcode = MOpcode::Alu;
        bool isVector = rest.find(".4s") != std::string::npos ||
                        rest.find(".2d") != std::string::npos;
        if (op == "fadd" || op == "fsub")
            mi.latency = isVector ? 1 : 4;
        else if (op == "fneg" || op == "scvtf" || op == "fcvtzs")
            mi.latency = 4;
        else
            mi.latency = 1;
        defFirstUseRest();
    } else {
        mi.opcode = MOpcode::Unknown;
        defFirstUseRest();
        if (rest.find('[') != std::string::npos)
            mi.isBarrier = true;
    }

    annotateMemoryInfo(mi, operands, op);

    if (touchesStackPointer(mi) || touchesLinkOrFrameCritical(mi))
        mi.isBarrier = true;

    return mi;
}

void appendMachineInstr(MachineFunction &func, MachineInstr inst) {
    if (func.blocks.empty())
        func.blocks.push_back({});

    inst.originalIndex = func.nextIndex++;

    if (inst.opcode == MOpcode::Label && !func.blocks.back().instrs.empty()) {
        func.blocks.push_back({});
        func.blocks.back().label = inst.text;
    }
    func.blocks.back().instrs.push_back(std::move(inst));
}

std::string printMachineFunction(const MachineFunction &func) {
    std::ostringstream out;
    for (const auto &block : func.blocks) {
        for (const auto &inst : block.instrs)
            out << inst.text << "\n";
    }
    return out.str();
}

namespace {

const char *mopcodeToString(MOpcode op) {
    switch (op) {
        case MOpcode::Unknown:    return "Unknown";
        case MOpcode::Label:      return "Label";
        case MOpcode::Directive:  return "Directive";
        case MOpcode::Comment:    return "Comment";
        case MOpcode::Mov:        return "Mov";
        case MOpcode::Alu:        return "Alu";
        case MOpcode::Mul:        return "Mul";
        case MOpcode::Div:        return "Div";
        case MOpcode::Load:       return "Load";
        case MOpcode::Store:      return "Store";
        case MOpcode::PairLoad:   return "PairLoad";
        case MOpcode::PairStore:  return "PairStore";
        case MOpcode::Cmp:        return "Cmp";
        case MOpcode::FlagUse:    return "FlagUse";
        case MOpcode::Branch:     return "Branch";
        case MOpcode::Call:       return "Call";
        case MOpcode::Ret:        return "Ret";
        case MOpcode::Adr:        return "Adr";
        case MOpcode::Neon:       return "Neon";
    }
    return "?";
}

std::string joinSet(const std::set<std::string> &s) {
    std::string result;
    for (const auto &v : s) {
        if (!result.empty()) result += ", ";
        result += v;
    }
    return result.empty() ? "{}" : "{" + result + "}";
}

} // anonymous namespace

std::string dumpMachineFunction(const MachineFunction &func) {
    std::ostringstream out;
    out << "=== MachineFunction: " << func.name << " ===\n";
    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
        const auto &block = func.blocks[bi];
        out << "  BB" << bi;
        if (!block.label.empty()) out << " [" << block.label << "]";
        out << " (" << block.instrs.size() << " instrs)\n";
        for (const auto &inst : block.instrs) {
            out << "    [" << inst.originalIndex << "] "
                << mopcodeToString(inst.opcode) << " | "
                << "text=\"" << inst.text << "\" "
                << "defs=" << joinSet(inst.defs) << " "
                << "uses=" << joinSet(inst.uses) << " "
                << "lat=" << inst.latency;
            if (inst.mayLoad)    out << " LOAD";
            if (inst.mayStore)   out << " STORE";
            if (inst.setsFlags)  out << " SETS_FLAGS";
            if (inst.usesFlags)  out << " USES_FLAGS";
            if (inst.isCall)     out << " CALL";
            if (inst.isBarrier)  out << " BARRIER";
            if (inst.isLabelLike)out << " LABEL_LIKE";
            if (inst.memOffsetKnown) {
                out << " MEM[" << inst.memBase << "+"
                    << inst.memOffset << ":" << inst.memWidth << "]";
            }
            out << "\n";
        }
    }
    return out.str();
}

std::string printMachineModule(const MachineModule &module) {
    std::ostringstream out;
    for (const auto &inst : module.lines)
        out << inst.text << "\n";
    return out.str();
}

void appendMachineLine(MachineModule &module, const std::string &line) {
    MachineInstr inst = MachineInstr::raw(line);
    inst.originalIndex = module.nextIndex++;
    module.lines.push_back(std::move(inst));
}

void appendMachineText(MachineModule &module, const std::string &text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        appendMachineLine(module, line);
}

MachineEmitter::MachineEmitter(MachineFunction &func)
    : func_(func) {}

void MachineEmitter::emit(MachineInstr inst) {
    appendMachineInstr(func_, std::move(inst));
}

void MachineEmitter::emitLine(const std::string &line) {
    emit(MachineInstr::raw(line));
}
