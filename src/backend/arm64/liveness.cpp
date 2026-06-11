#include "../../include/backend/arm64/liveness.hpp"

#include <algorithm>
#include <cctype>
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

std::string labelName(std::string label) {
    label = trim(label);
    if (!label.empty() && label.back() == ':')
        label.pop_back();
    return label;
}

std::string mnemonic(const MachineInstr &inst) {
    std::string t = trim(inst.text);
    if (t.empty()) return "";
    std::istringstream in(t);
    std::string op;
    in >> op;
    return lower(op);
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

const MachineInstr *lastRealInstruction(const MachineBasicBlock &block) {
    for (auto it = block.instrs.rbegin(); it != block.instrs.rend(); ++it) {
        if (!it->isLabelLike && it->opcode != MOpcode::Comment &&
            it->opcode != MOpcode::Directive) {
            return &*it;
        }
    }
    return nullptr;
}

std::set<std::string> callerSavedRegs() {
    std::set<std::string> regs;
    for (int r = 0; r <= 18; ++r)
        regs.insert("r" + std::to_string(r));
    for (int r = 0; r <= 7; ++r)
        regs.insert("v" + std::to_string(r));
    for (int r = 16; r <= 31; ++r)
        regs.insert("v" + std::to_string(r));
    regs.insert(kMachineFlagsReg);
    return regs;
}

std::set<std::string> physicalBoundaryRegs() {
    std::set<std::string> regs;
    for (int r = 0; r <= 30; ++r)
        regs.insert("r" + std::to_string(r));
    for (int r = 0; r <= 31; ++r)
        regs.insert("v" + std::to_string(r));
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
        auto clobbers = callerSavedRegs();
        defs.insert(clobbers.begin(), clobbers.end());
    }

    return defs;
}

} // namespace

std::vector<std::vector<size_t>>
MachineLiveness::computeSuccessors(const MachineFunction &func) const {
    std::map<std::string, size_t> labelToBlock;
    for (size_t i = 0; i < func.blocks.size(); ++i) {
        if (!func.blocks[i].label.empty())
            labelToBlock[labelName(func.blocks[i].label)] = i;
        for (const auto &inst : func.blocks[i].instrs) {
            if (inst.opcode == MOpcode::Label) {
                labelToBlock[labelName(inst.text)] = i;
                break;
            }
        }
    }

    std::vector<std::vector<size_t>> succs(func.blocks.size());
    for (size_t i = 0; i < func.blocks.size(); ++i) {
        const MachineInstr *term = lastRealInstruction(func.blocks[i]);
        auto addFallthrough = [&]() {
            if (i + 1 < func.blocks.size())
                succs[i].push_back(i + 1);
        };
        auto addTarget = [&](const std::string &target) {
            auto it = labelToBlock.find(labelName(target));
            if (it != labelToBlock.end())
                succs[i].push_back(it->second);
        };

        if (!term) {
            addFallthrough();
            continue;
        }

        std::string op = mnemonic(*term);
        auto ops = operands(*term);

        if (term->opcode == MOpcode::Ret) {
            continue;
        } else if (term->opcode == MOpcode::Branch) {
            if (op == "b") {
                if (!ops.empty()) addTarget(ops[0]);
            } else if (op == "cbz" || op == "cbnz") {
                if (ops.size() >= 2) addTarget(ops[1]);
                addFallthrough();
            } else {
                if (!ops.empty()) addTarget(ops[0]);
                addFallthrough();
            }
        } else {
            addFallthrough();
        }

        std::sort(succs[i].begin(), succs[i].end());
        succs[i].erase(std::unique(succs[i].begin(), succs[i].end()), succs[i].end());
    }
    return succs;
}

MachineLivenessResult MachineLiveness::analyze(const MachineFunction &func) const {
    MachineLivenessResult result;
    const size_t n = func.blocks.size();
    result.blockLiveIn.resize(n);
    result.blockLiveOut.resize(n);

    auto succs = computeSuccessors(func);
    std::vector<std::set<std::string>> blockUse(n), blockDef(n);

    for (size_t bi = 0; bi < n; ++bi) {
        for (const auto &inst : func.blocks[bi].instrs) {
            auto uses = effectiveUses(inst);
            auto defs = effectiveDefs(inst);

            for (const auto &reg : uses) {
                if (!blockDef[bi].count(reg))
                    blockUse[bi].insert(reg);
            }
            blockDef[bi].insert(defs.begin(), defs.end());
        }
    }

    bool changed;
    do {
        changed = false;
        for (size_t bi = n; bi > 0; --bi) {
            size_t i = bi - 1;
            std::set<std::string> newOut;
            for (size_t succ : succs[i]) {
                newOut.insert(result.blockLiveIn[succ].begin(),
                              result.blockLiveIn[succ].end());
            }

            std::set<std::string> newIn = blockUse[i];
            for (const auto &reg : newOut) {
                if (!blockDef[i].count(reg))
                    newIn.insert(reg);
            }

            if (newIn != result.blockLiveIn[i] || newOut != result.blockLiveOut[i]) {
                changed = true;
                result.blockLiveIn[i] = std::move(newIn);
                result.blockLiveOut[i] = std::move(newOut);
            }
        }
    } while (changed);

    const auto boundaryRegs = physicalBoundaryRegs();
    for (size_t bi = 0; bi < n; ++bi) {
        std::set<std::string> live = result.blockLiveOut[bi];
        // MachineBasicBlock currently means "label-delimited region", not a
        // true single-entry/single-exit basic block. Keep physical registers
        // conservatively live at region boundaries so DCE only removes values
        // proven dead inside the region.
        live.insert(boundaryRegs.begin(), boundaryRegs.end());
        const auto &instrs = func.blocks[bi].instrs;
        for (auto it = instrs.rbegin(); it != instrs.rend(); ++it) {
            result.instrLiveOut[&*it] = live;
            auto defs = effectiveDefs(*it);
            auto uses = effectiveUses(*it);
            for (const auto &reg : defs)
                live.erase(reg);
            live.insert(uses.begin(), uses.end());
        }
    }

    return result;
}
