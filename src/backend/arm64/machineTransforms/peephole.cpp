#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <vector>




// ── tryMachineSelfMove: used only by runMachinePeephole ────────────

static bool tryMachineSelfMove(MachineBasicBlock &block, size_t idx) {
	auto &inst = block.instrs[idx];
	const MachineInstr &line = inst;
	if (line.isLabelLike) return false;
	if (line.opcodeText != "mov" && line.opcodeText != "fmov") return false;
	if (line.rawOperands.size() < 2) return false;
	if (line.rawOperands[0] != line.rawOperands[1]) return false;

	block.instrs.erase(block.instrs.begin() + idx);
	return true;
}

bool runMachinePeephole(MachineFunction &func) {
	for (auto &block : func.blocks) {
		for (size_t i = 0; i < block.instrs.size(); ++i) {
			if (tryMachineSelfMove(block, i))
				return true;
		}
	}
	return false;
}
