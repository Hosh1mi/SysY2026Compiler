#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cctype>
#include <string>
#include <vector>

static std::string parseFullVectorListReg(const std::string &operand) {
	std::string text = peephTrim(operand);
	if (text.size() < 7 || text.front() != '{' || text.back() != '}')
		return "";
	text = peephTrim(text.substr(1, text.size() - 2));
	if (text.empty() || text[0] != 'v')
		return "";

	size_t pos = 1;
	while (pos < text.size() && std::isdigit(text[pos]))
		++pos;
	if (pos == 1 || pos >= text.size() || text[pos] != '.')
		return "";

	std::string suffix = text.substr(pos);
	if (suffix != ".16b" && suffix != ".8h" && suffix != ".4s" && suffix != ".2d")
		return "";
	return "q" + text.substr(1, pos - 1);
}

static bool tryMachineVectorLdStAlias(MachineBasicBlock &block, size_t idx) {
	const MachineInstr &line = block.instrs[idx];
	if (line.isLabelLike)
		return false;
	if (line.opcodeText != "ld1" && line.opcodeText != "st1")
		return false;
	if (line.rawOperands.size() != 2 && line.rawOperands.size() != 3)
		return false;

	std::string qReg = parseFullVectorListReg(line.rawOperands[0]);
	if (qReg.empty())
		return false;

	MemOperand addr = peephParseMemOp(line.rawOperands[1]);
	if (!addr.valid || addr.offset != 0)
		return false;
	if (line.rawOperands.size() == 3 && line.rawOperands[2] != "#16")
		return false;

	std::vector<std::string> operands = {qReg, line.rawOperands[1]};
	if (line.rawOperands.size() == 3)
		operands.push_back(line.rawOperands[2]);
	peephReplaceInstr(block.instrs[idx],
	                    peephMakeInsn(line.opcodeText == "ld1" ? "ldr" : "str",
	                                    operands));
	return true;
}

bool runMachineCanonicalization(MachineFunction &func) {
	for (auto &block : func.blocks)
		for (size_t i = 0; i < block.instrs.size(); ++i)
			if (tryMachineVectorLdStAlias(block, i))
				return true;
	return false;
}
