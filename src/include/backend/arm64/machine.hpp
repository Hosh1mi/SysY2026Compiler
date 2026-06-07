#pragma once

#include <initializer_list>
#include <ostream>
#include <set>
#include <streambuf>
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

MachineInstr parseMachineInstr(const std::string &line, int originalIndex);
std::string printMachineFunction(const MachineFunction &func);
void appendMachineInstr(MachineFunction &func, MachineInstr inst);

class MachineStreamBuf : public std::streambuf {
public:
    explicit MachineStreamBuf(MachineFunction &func);
    ~MachineStreamBuf() override;

protected:
    int overflow(int ch) override;
    std::streamsize xsputn(const char *s, std::streamsize n) override;
    int sync() override;

private:
    void append(char ch);
    void flushLine();

    MachineFunction &func_;
    std::string line_;
    int lineIndex_ = 0;
};

class MachineEmitter {
public:
    explicit MachineEmitter(MachineFunction &func);

    std::ostream &stream();
    void emit(MachineInstr inst);
    void emitLine(const std::string &line);

private:
    MachineFunction &func_;
    MachineStreamBuf streamBuf_;
    std::ostream stream_;
};
