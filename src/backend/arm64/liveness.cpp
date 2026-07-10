#include "../../include/backend/arm64/liveness.hpp"

#include <algorithm>
#include <cctype>

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

std::string labelName(std::string label) {
    label = trim(label);
    if (!label.empty() && label.back() == ':')
        label.pop_back();
    return label;
}

std::string mnemonic(const MachineInstr &inst) {
    std::string t = trim(inst.text);
    if (t.empty()) return "";
    size_t sp = t.find_first_of(" \t");                                           
    return sp == std::string::npos ? lower(t) : lower(t.substr(0, sp));  
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

std::vector<std::string> operands(const MachineInstr &inst) {
    std::string t = trim(inst.text);
    size_t sp = t.find_first_of(" \t");
    if (sp == std::string::npos) return {};
    return splitOperands(t.substr(sp + 1));
}

const std::set<std::string> &callerSavedRegs() {                                  
    static const std::set<std::string> regs = []() {                              
        std::set<std::string> r;                                                  
        for (int i = 0; i <= 18; ++i)  r.insert("r" + std::to_string(i));         
        for (int i = 0; i <= 7; ++i)   r.insert("v" + std::to_string(i));         
        for (int i = 16; i <= 31; ++i) r.insert("v" + std::to_string(i));         
        r.insert(kMachineFlagsReg);                                               
        return r;                                                                 
    }(); 
    return regs;
}

const std::set<std::string> &physicalBoundaryRegs() {                             
    static const std::set<std::string> regs = []() {                              
        std::set<std::string> r;                                                  
        for (int i = 0; i <= 30; ++i) r.insert("r" + std::to_string(i));          
        for (int i = 0; i <= 31; ++i) r.insert("v" + std::to_string(i));          
        return r;                                                                 
    }(); 
    return regs;
}

std::set<std::string> effectiveUses(const MachineInstr &inst) {
    std::set<std::string> uses = inst.uses;

    if (inst.usesFlags)
        uses.insert(kMachineFlagsReg);

    if (inst.opcode == MOpcode::Neon && inst.text.find(".s[") != std::string::npos) {
        uses.insert(inst.defs.begin(), inst.defs.end());
    }

    if (inst.opcode == MOpcode::Call) {
        for (int r = 0; r <= 7; ++r) {
            uses.insert("r" + std::to_string(r));
            uses.insert("v" + std::to_string(r));
        }
    }

    if (inst.opcode == MOpcode::Ret) {
        // Keep ABI return registers live at function exit. This is conservative
        // for void functions but prevents deleting the final return-value move.
        uses.insert("r0");
        uses.insert("v0");
    }

    return uses;
}

std::set<std::string> effectiveDefs(const MachineInstr &inst) {
    std::set<std::string> defs = inst.defs;

    if (inst.setsFlags)
        defs.insert(kMachineFlagsReg);

    if (inst.opcode == MOpcode::Call) {
        const auto &clobbers = callerSavedRegs();
        defs.insert(clobbers.begin(), clobbers.end());
    }

    return defs;
}

} // namespace

MachineLivenessResult MachineLiveness::analyze(const MachineFunction &func) const {
    MachineLivenessResult result;
    const size_t n = func.blocks.size();
    result.blockLiveIn.resize(n);
    result.blockLiveOut.resize(n);

    struct InstrRef {
        const MachineInstr *inst;
    };
    std::vector<InstrRef> flat;
    std::map<std::string, size_t> labelToInstr;
    std::vector<size_t> blockFirst(n, static_cast<size_t>(-1));
    std::vector<size_t> blockLast(n, static_cast<size_t>(-1));

    for (size_t bi = 0; bi < n; ++bi) {
        for (const auto &inst : func.blocks[bi].instrs) {
            size_t index = flat.size();
            if (blockFirst[bi] == static_cast<size_t>(-1))
                blockFirst[bi] = index;
            blockLast[bi] = index;
            flat.push_back({&inst});
            if (inst.opcode == MOpcode::Label)
                labelToInstr[labelName(inst.text)] = index;
        }
    }

    std::vector<std::vector<size_t>> instrSuccs(flat.size());
    std::vector<bool> unknownSuccessor(flat.size(), false);
    for (size_t i = 0; i < flat.size(); ++i) {
        const MachineInstr &inst = *flat[i].inst;
        auto addNext = [&]() {
            if (i + 1 < flat.size())
                instrSuccs[i].push_back(i + 1);
        };
        auto addTarget = [&](const std::string &target) {
            auto it = labelToInstr.find(labelName(target));
            if (it != labelToInstr.end())
                instrSuccs[i].push_back(it->second);
            else
                unknownSuccessor[i] = true;
        };

        if (inst.opcode == MOpcode::Ret) {
            continue;
        }
        if (inst.opcode != MOpcode::Branch) {
            addNext();
            continue;
        }

        std::string op = mnemonic(inst);
        auto ops = operands(inst);
        if (op == "b") {
            if (!ops.empty())
                addTarget(ops[0]);
            else
                unknownSuccessor[i] = true;
        } else if (op == "cbz" || op == "cbnz") {
            if (ops.size() >= 2)
                addTarget(ops[1]);
            else
                unknownSuccessor[i] = true;
            addNext();
        } else if (op == "tbz" || op == "tbnz") {
            if (ops.size() >= 3)
                addTarget(ops[2]);
            else
                unknownSuccessor[i] = true;
            addNext();
        } else {
            if (!ops.empty())
                addTarget(ops[0]);
            else
                unknownSuccessor[i] = true;
            addNext();
        }
    }

    std::vector<std::set<std::string>> instrLiveIn(flat.size());
    std::vector<std::set<std::string>> instrLiveOut(flat.size());
    const auto &boundaryRegs = physicalBoundaryRegs();
    bool changed;
    do {
        changed = false;
        for (size_t ii = flat.size(); ii > 0; --ii) {
            size_t i = ii - 1;
            std::set<std::string> newOut;
            for (size_t succ : instrSuccs[i]) {
                newOut.insert(instrLiveIn[succ].begin(),
                              instrLiveIn[succ].end());
            }
            if (unknownSuccessor[i])
                newOut.insert(boundaryRegs.begin(), boundaryRegs.end());

            std::set<std::string> newIn = effectiveUses(*flat[i].inst);
            auto defs = effectiveDefs(*flat[i].inst);
            for (const auto &reg : newOut) {
                if (!defs.count(reg))
                    newIn.insert(reg);
            }

            if (newIn != instrLiveIn[i] || newOut != instrLiveOut[i]) {
                changed = true;
                instrLiveIn[i] = std::move(newIn);
                instrLiveOut[i] = std::move(newOut);
            }
        }
    } while (changed);

    for (size_t i = 0; i < flat.size(); ++i) {
        result.instrLiveOut[flat[i].inst] = instrLiveOut[i];
        if (flat[i].inst->opcode == MOpcode::Label)
            result.labelLiveIn[labelName(flat[i].inst->text)] = instrLiveIn[i];
    }

    for (size_t bi = 0; bi < n; ++bi) {
        if (blockFirst[bi] == static_cast<size_t>(-1))
            continue;
        result.blockLiveIn[bi] = instrLiveIn[blockFirst[bi]];
        result.blockLiveOut[bi] = instrLiveOut[blockLast[bi]];
    }

    return result;
}
