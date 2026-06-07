#pragma once

#include <set>
#include <string>
#include <vector>

enum class MOpcode {
    Unknown,
    Label,
    Directive,
    Comment,
    Mov,
    Alu,
    Mul,
    Div,
    Load,
    Store,
    PairLoad,
    PairStore,
    Cmp,
    FlagUse,
    Branch,
    Call,
    Ret,
    Adr,
    Neon,
};

struct MachineInstr {
    std::string text;
    std::string opcodeText;
    MOpcode opcode = MOpcode::Unknown;

    std::set<std::string> defs;
    std::set<std::string> uses;

    bool mayLoad = false;
    bool mayStore = false;
    bool setsFlags = false;
    bool usesFlags = false;
    bool isBarrier = false;
    bool isLabelLike = false;
    int latency = 1;
    int originalIndex = 0;
};

struct MachineBasicBlock {
    std::string label;
    std::vector<MachineInstr> instrs;
};

struct MachineFunction {
    std::string name;
    std::vector<MachineBasicBlock> blocks;
};

MachineInstr parseMachineInstr(const std::string &line, int originalIndex);
