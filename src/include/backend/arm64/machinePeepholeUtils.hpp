#pragma once

#include "machine.hpp"
#include "liveness.hpp"
#include <string>
#include <vector>

struct MemOperand {
	std::string base;
	int offset = 0;
	bool valid = false;
};

std::string peephTrim(const std::string &s);
char peephRegClass(const std::string &r);
int peephRegSize(char cls);
bool peephParsePhysicalReg(const std::string &reg, char &cls, std::string &num);
bool peephSamePhysicalReg(const std::string &a, const std::string &b);
MemOperand peephParseMemOp(const std::string &s);
std::string peephMakeInsn(const std::string &mnemonic,
                          const std::vector<std::string> &operands);
void peephReplaceInstr(MachineInstr &inst, const std::string &text);
bool peephIsInertLine(const MachineInstr &inst);
bool peephLineReadsReg(const MachineInstr &l, const std::string &r);
bool peephLineUsesReg(const MachineInstr &l, const std::string &r);
bool peephLineWritesReg(const MachineInstr &l, const std::string &r);
bool peephRegDeadAfter(const MachineBasicBlock &block, size_t idx,
                       const std::string &reg,
                       const MachineLivenessResult &liveness);
std::vector<size_t> peephInstrWindow(const MachineBasicBlock &block,
                                     size_t idx, int count);
bool peephIsControlFlowBarrier(const MachineInstr &inst);
