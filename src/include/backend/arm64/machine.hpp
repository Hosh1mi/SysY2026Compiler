#pragma once

#include <initializer_list>
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
    bool isCall = false;
    bool isBarrier = false;
    bool isLabelLike = false;
    int latency = 1;
    int originalIndex = 0;

    // Conservative memory summary for postRA scheduling.  The range is only
    // considered precise when memOffsetKnown is true; otherwise memory ops are
    // treated as possibly aliasing any other memory op.
    std::string memBase;
    bool memOffsetKnown = false;
    int memOffset = 0;
    int memWidth = 0;

    static MachineInstr raw(const std::string &line);
    static MachineInstr make(const std::string &line, MOpcode opcode,
                             std::initializer_list<std::string> defs = {},
                             std::initializer_list<std::string> uses = {},
                             int latency = 1);
};

struct MachineBasicBlock {
    std::string label;
    std::vector<MachineInstr> instrs;
};

struct MachineFunction {
    std::string name;
    std::vector<MachineBasicBlock> blocks;
    int nextIndex = 0;
};

struct MachineModule {
    std::vector<MachineInstr> lines;
    int nextIndex = 0;
};

MachineInstr parseMachineInstr(const std::string &line, int originalIndex);
std::string printMachineFunction(const MachineFunction &func);
std::string printMachineModule(const MachineModule &module);
std::string dumpMachineFunction(const MachineFunction &func);
void appendMachineInstr(MachineFunction &func, MachineInstr inst);
void appendMachineLine(MachineModule &module, const std::string &line);
void appendMachineText(MachineModule &module, const std::string &text);

class MachineEmitter {
public:
    explicit MachineEmitter(MachineFunction &func);

    void emit(MachineInstr inst);
    void emitLine(const std::string &line);

private:
    MachineFunction &func_;
};
