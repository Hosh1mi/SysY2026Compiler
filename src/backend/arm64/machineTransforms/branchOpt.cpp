#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

// Convert an extracted low-bit test into a bit-test branch:
//
//   and wN, wX, #1
//   cbz/cbnz wN, label
//
// becomes:
//
//   tbz/tbnz wX, #0, label
//
// The same rule also handles the equivalent tst/cmp + b.eq/b.ne forms, and
// the x-register variant.
static bool tryMachineAndTBZ(MachineBasicBlock &block, size_t idx,
                             const MachineLivenessResult &liveness) {
	const MachineInstr &andLine = block.instrs[idx];
	if (andLine.isLabelLike) return false;
	if (andLine.opcodeText != "and") return false;
	if (andLine.rawOperands.size() != 3) return false;
	if (andLine.rawOperands[2] != "#1") return false;

	const std::string &tempReg = andLine.rawOperands[0];
	const std::string &srcReg  = andLine.rawOperands[1];
	char cls = peephRegClass(tempReg);
	if (cls != 'w' && cls != 'x') return false;
	if (peephRegClass(srcReg) != cls) return false;

	// Scan forward for a consumer of tempReg.
	// We stop at barriers (calls, CF changes, writes to tempReg).
	enum { MaxScan = 8 };
	size_t scanEnd = std::min(idx + MaxScan, block.instrs.size());

	for (size_t i = idx + 1; i < scanEnd; ++i) {
		const MachineInstr &line = block.instrs[i];
		if (line.isLabelLike) continue;

		if (line.isCall) return false;

		// tempReg must not be redefined before consumption
		if (peephLineWritesReg(line, tempReg)) return false;

		// ── Direct cbz / cbnz ──────────────────────────────────
		bool isCbz  = (line.opcodeText == "cbz"  && line.rawOperands.size() >= 2 &&
		               line.rawOperands[0] == tempReg);
		bool isCbnz = (line.opcodeText == "cbnz" && line.rawOperands.size() >= 2 &&
		               line.rawOperands[0] == tempReg);

		// Bail on other control-flow barriers (unrelated cbz/cbnz, ret, b, etc.)
		if (!isCbz && !isCbnz && peephIsControlFlowBarrier(line))
			return false;

		if (isCbz || isCbnz) {
			// Make sure tempReg isn't read by any instruction between
			// the and and the cbz/cbnz (other than the cbz/cbnz itself).
			bool usedElsewhere = false;
			for (size_t j = idx + 1; j < i; ++j) {
				const MachineInstr &between = block.instrs[j];
				if (between.isLabelLike) continue;
				if (peephLineReadsReg(between, tempReg)) { usedElsewhere = true; break; }
			}
			if (usedElsewhere) return false;

			const std::string &label = line.rawOperands[1];
			std::string newMnemonic = isCbz ? "tbz" : "tbnz";
			peephReplaceInstr(block.instrs[i],
			                    peephMakeInsn(newMnemonic,
			                                    {srcReg, "#0", label}));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}

		// ── tst wN, wN  or  cmp wN, #0 → b.eq / b.ne ─────────
		bool isTst = (line.opcodeText == "tst" && line.rawOperands.size() >= 2 &&
		              line.rawOperands[0] == tempReg && line.rawOperands[1] == tempReg);
		bool isCmpZero = (line.opcodeText == "cmp" && line.rawOperands.size() >= 2 &&
		                  line.rawOperands[0] == tempReg &&
		                  line.rawOperands[1] == "#0");

		if (!isTst && !isCmpZero) {
			// tempReg is used by some other instruction — bail out
			if (peephLineUsesReg(line, tempReg)) return false;
			continue;
		}

		// tempReg should not be read by other instructions between and → tst/cmp
		bool usedElsewhere = false;
		for (size_t j = idx + 1; j < i; ++j) {
			const MachineInstr &between = block.instrs[j];
			if (between.isLabelLike) continue;
			if (peephLineReadsReg(between, tempReg)) { usedElsewhere = true; break; }
		}
		if (usedElsewhere) return false;

		// Now look for b.eq / b.ne immediately following (skipping non-instructions)
		size_t branchIdx = i + 1;
		while (branchIdx < block.instrs.size()) {
			const MachineInstr &brLine = block.instrs[branchIdx];
			if (peephIsInertLine(brLine)) {
				++branchIdx;
				continue;
			}
			if (brLine.isLabelLike) return false;

			// Flags must not be clobbered between tst/cmp and the branch
			for (size_t k = i + 1; k < branchIdx; ++k) {
				const MachineInstr &between = block.instrs[k];
				if (between.isLabelLike) continue;
				if (between.setsFlags) return false;
				if (between.opcodeText == "b" || between.opcodeText == "bl" ||
				    between.opcodeText == "blr" || between.opcodeText == "ret")
					return false;
			}

			bool isSupportedBranch =
				(brLine.opcodeText == "b.eq" || brLine.opcodeText == "b.ne") &&
				brLine.rawOperands.size() >= 1;
			if (isSupportedBranch) {
				auto liveIt = liveness.instrLiveOut.find(&block.instrs[branchIdx]);
				if (liveIt == liveness.instrLiveOut.end() ||
				    liveIt->second.count(kMachineFlagsReg))
					return false;
			}

			if (brLine.opcodeText == "b.eq" && brLine.rawOperands.size() >= 1) {
				peephReplaceInstr(block.instrs[branchIdx],
				                    peephMakeInsn("tbz",
				                                    {srcReg, "#0", brLine.rawOperands[0]}));
				block.instrs.erase(block.instrs.begin() + i);   // remove tst/cmp
				block.instrs.erase(block.instrs.begin() + idx); // remove and
				return true;
			}
			if (brLine.opcodeText == "b.ne" && brLine.rawOperands.size() >= 1) {
				peephReplaceInstr(block.instrs[branchIdx],
				                    peephMakeInsn("tbnz",
				                                    {srcReg, "#0", brLine.rawOperands[0]}));
				block.instrs.erase(block.instrs.begin() + i);   // remove tst/cmp
				block.instrs.erase(block.instrs.begin() + idx); // remove and
				return true;
			}
			return false;
		}
		return false;
	}
	return false;
}

static std::string labelName(const MachineInstr &inst) {
	const MachineInstr &line = inst;
	if (line.opcode != MOpcode::Label) return "";
	std::string label = peephTrim(line.text);
	if (!label.empty() && label.back() == ':')
		label.pop_back();
	return label;
}

static bool isReturnReg(const std::string &reg) {
	return reg == "w0" || reg == "x0" || reg == "s0" || reg == "d0";
}

static bool isUncondBranchTo(const MachineInstr &inst, std::string &target) {
	if (inst.opcodeText != "b" || inst.rawOperands.size() != 1)
		return false;
	target = inst.rawOperands[0];
	return true;
}

static bool isDirectCall(const MachineInstr &inst, std::string &target) {
	if (inst.opcodeText != "bl" || inst.rawOperands.size() != 1)
		return false;
	target = inst.rawOperands[0];
	return !target.empty();
}

static int findBlockByLabel(const MachineFunction &func, const std::string &label) {
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		if (!func.blocks[b].instrs.empty() &&
		    labelName(func.blocks[b].instrs.front()) == label)
			return static_cast<int>(b);
	}
	return -1;
}

static bool collectEpilogueBody(const MachineFunction &func,
                                std::vector<MachineInstr> &body) {
	const std::string epilogueLabel = ".L" + func.name + "_epilogue";
	int epilogueBlock = findBlockByLabel(func, epilogueLabel);
	if (epilogueBlock < 0)
		return false;

	const auto &instrs = func.blocks[epilogueBlock].instrs;
	if (instrs.size() < 2)
		return false;
	if (instrs.back().opcodeText != "ret")
		return false;

	body.clear();
	for (size_t i = 1; i + 1 < instrs.size(); ++i) {
		const MachineInstr &line = instrs[i];
		if (line.isLabelLike)
			return false;
		if (line.opcodeText == "b" || line.opcodeText == "bl" ||
		    line.opcodeText == "blr" || line.opcodeText == "ret" ||
		    line.opcodeText == "br")
			return false;
		body.push_back(instrs[i]);
	}
	return true;
}

static bool matchReturnForwarder(const MachineFunction &func,
                                 const std::string &label,
                                 const std::string &tempReg) {
	int blockIdx = findBlockByLabel(func, label);
	if (blockIdx < 0)
		return false;
	const auto &instrs = func.blocks[blockIdx].instrs;
	if (instrs.size() != 3)
		return false;

	const MachineInstr &move = instrs[1];
	if ((move.opcodeText != "mov" && move.opcodeText != "fmov") ||
	    move.rawOperands.size() != 2 ||
	    !isReturnReg(move.rawOperands[0]) ||
	    !peephSamePhysicalReg(move.rawOperands[1], tempReg))
		return false;

	std::string branchTarget;
	if (!isUncondBranchTo(instrs[2], branchTarget))
		return false;
	return branchTarget == ".L" + func.name + "_epilogue";
}

static bool nextVisibleIsLabel(const MachineFunction &func,
                               size_t blockIdx,
                               size_t instrIdx,
                               const std::string &target) {
	for (size_t b = blockIdx; b < func.blocks.size(); ++b) {
		const auto &instrs = func.blocks[b].instrs;
		size_t begin = (b == blockIdx) ? instrIdx + 1 : 0;
		for (size_t i = begin; i < instrs.size(); ++i) {
			const MachineInstr &line = instrs[i];
			if (peephIsInertLine(line))
				continue;
			if (line.opcode == MOpcode::Label)
				return labelName(instrs[i]) == target;
			return false;
		}
	}
	return false;
}

// 分支指令的跳转目标操作数下标（目标总是最后一个操作数）；非分支返回 -1。
// 注意 "bl" 是调用不是分支。
static int branchTargetOperandIndex(const MachineInstr &line) {
	const std::string &m = line.opcodeText;
	if (m == "b" && line.rawOperands.size() == 1) return 0;
	if (m.size() > 2 && m.compare(0, 2, "b.") == 0 && line.rawOperands.size() == 1)
		return 0;
	if ((m == "cbz" || m == "cbnz") && line.rawOperands.size() == 2) return 1;
	if ((m == "tbz" || m == "tbnz") && line.rawOperands.size() == 3) return 2;
	return -1;
}

// 找到函数内名为 target 的 label 后第一条会被执行的指令
// （跳过空行/注释/连续 label——顺序执行会穿过它们）；找不到或遇到
// directive 等不可分析内容返回 false。
static bool findFirstInstrAfterLabel(const MachineFunction &func,
                                     const std::string &target,
                                     size_t &outBlock, size_t &outInstr) {
	bool seen = false;
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		const auto &instrs = func.blocks[b].instrs;
		for (size_t i = 0; i < instrs.size(); ++i) {
			const MachineInstr &line = instrs[i];
			if (!seen) {
				if (line.opcode == MOpcode::Label && labelName(instrs[i]) == target)
					seen = true;
				continue;
			}
			if (peephIsInertLine(line) ||
			    line.opcode == MOpcode::Label)
				continue;
			if (line.isLabelLike)
				return false;
			outBlock = b;
			outInstr = i;
			return true;
		}
	}
	return false;
}

// 沿 forwarder 链（label 处第一条指令是无条件 b）解析出最终跳转目标；
// 遇环返回空串。
static std::string resolveForwardTarget(const MachineFunction &func,
                                        std::string target) {
	std::set<std::string> visited;
	while (visited.insert(target).second) {
		size_t b = 0, i = 0;
		if (!findFirstInstrAfterLabel(func, target, b, i))
			return target;
		const MachineInstr &line = func.blocks[b].instrs[i];
		if (line.opcodeText != "b" || line.rawOperands.size() != 1)
			return target;
		target = line.rawOperands[0];
	}
	return "";
}

// 跳转链折叠：分支目标若是只含一条无条件 b 的 forwarder 块，直接改跳最终目标。
//   b.eq .Ledge_1        →  b.eq real_target
//   .Ledge_1: b real_target
static bool tryMachineBranchThreading(MachineFunction &func,
                                      size_t blockIdx,
                                      size_t instrIdx) {
	auto &inst = func.blocks[blockIdx].instrs[instrIdx];
	const MachineInstr &line = inst;
	if (line.isLabelLike) return false;
	int ti = branchTargetOperandIndex(line);
	if (ti < 0) return false;
	const std::string target = line.rawOperands[ti];
	std::string finalTarget = resolveForwardTarget(func, target);
	if (finalTarget.empty() || finalTarget == target) return false;

	auto newBranchOps = line.rawOperands;
	newBranchOps[ti] = finalTarget;
	peephReplaceInstr(inst, peephMakeInsn(line.opcodeText, newBranchOps));
	return true;
}

// 清理跳转链折叠后遗留的不可达 forwarder 残块：
// label 是函数内部标签、未被函数内任何操作数引用、前一条可见指令是
// 无条件 b/ret（无 fallthrough 进入）、其后紧跟一条无条件 b 时，
// 删除该 label 与这条 b。
static bool tryMachineRemoveDeadForwarder(MachineFunction &func,
                                          size_t blockIdx,
                                          size_t instrIdx) {
	auto &block = func.blocks[blockIdx];
	const MachineInstr &labelLine = block.instrs[instrIdx];
	if (labelLine.opcode != MOpcode::Label) return false;
	std::string label = labelName(block.instrs[instrIdx]);
	if (label.empty() || label == func.name) return false;
	// 仅处理本函数生成的内部标签，避免触碰任何可能被外部引用的符号
	if (label.compare(0, 2, ".L") != 0 &&
	    (label.size() <= func.name.size() + 1 ||
	     label.compare(0, func.name.size() + 1, func.name + "_") != 0))
		return false;

	// 函数内任何指令的任何操作数都不得引用该 label
	for (const auto &bb : func.blocks) {
		for (const auto &other : bb.instrs) {
			const MachineInstr &l = other;
			if (l.isLabelLike) continue;
			for (const auto &op : l.rawOperands)
				if (op == label) return false;
		}
	}

	// 前一条可见指令必须是无条件 b 或 ret（否则存在 fallthrough 进入）
	{
		bool ok = false;
		for (size_t b = blockIdx + 1; b-- > 0;) {
			const auto &instrs = func.blocks[b].instrs;
			size_t start = (b == blockIdx) ? instrIdx : instrs.size();
			for (size_t i = start; i-- > 0;) {
				const MachineInstr &l = instrs[i];
				if (peephIsInertLine(l))
					continue;
				if (!l.isLabelLike &&
				    ((l.opcodeText == "b" && l.rawOperands.size() == 1) ||
				     l.opcodeText == "ret"))
					ok = true;
				goto prevChecked;
			}
		}
	prevChecked:
		if (!ok) return false;
	}

	// 其后第一条可见行必须就是无条件 b（中间不允许有其他 label）
	size_t bIdx = blockIdx, iIdx = instrIdx + 1;
	for (; bIdx < func.blocks.size(); ++bIdx, iIdx = 0) {
		auto &instrs = func.blocks[bIdx].instrs;
		for (; iIdx < instrs.size(); ++iIdx) {
			const MachineInstr &l = instrs[iIdx];
			if (peephIsInertLine(l))
				continue;
			if (!l.isLabelLike && l.opcodeText == "b" &&
			    l.rawOperands.size() == 1) {
				func.blocks[bIdx].instrs.erase(func.blocks[bIdx].instrs.begin() + iIdx);
				block.instrs.erase(block.instrs.begin() + instrIdx);
				return true;
			}
			return false;
		}
	}
	return false;
}

static bool tryMachineFallthroughBranch(MachineFunction &func,
                                        size_t blockIdx,
                                        size_t instrIdx) {
	auto &block = func.blocks[blockIdx];
	auto &inst = block.instrs[instrIdx];
	const MachineInstr &line = inst;
	if (line.isLabelLike) return false;
	if (line.opcodeText != "b" || line.rawOperands.size() != 1) return false;
	if (!nextVisibleIsLabel(func, blockIdx, instrIdx, line.rawOperands[0])) return false;

	block.instrs.erase(block.instrs.begin() + instrIdx);
	return true;
}

// A fallthrough edge that copies into a shared return block can return
// directly without first materializing the shared block's input register:
//
//   mov temp, src; return_label: mov result, temp; ret
//     -> mov result, src; ret; return_label: mov result, temp; ret
static bool tryMachineFoldCopyIntoReturn(MachineFunction &func,
                                         size_t blockIdx,
                                         size_t instrIdx) {
	auto &block = func.blocks[blockIdx];
	const MachineInstr &copy = block.instrs[instrIdx];
	if (copy.isLabelLike) return false;
	if (copy.opcodeText != "mov" && copy.opcodeText != "fmov") return false;
	if (copy.rawOperands.size() != 2) return false;
	const std::string tempReg = copy.rawOperands[0];
	const std::string srcReg = copy.rawOperands[1];
	if (!peephRegClass(tempReg) || peephRegClass(tempReg) != peephRegClass(srcReg)) return false;

	size_t returnMoveBlock = 0, returnMoveInstr = 0;
	bool crossedLabel = false;
	bool foundReturnMove = false;
	for (size_t b = blockIdx; b < func.blocks.size() && !foundReturnMove; ++b) {
		const auto &instrs = func.blocks[b].instrs;
		size_t begin = (b == blockIdx) ? instrIdx + 1 : 0;
		for (size_t i = begin; i < instrs.size(); ++i) {
			const MachineInstr &line = instrs[i];
			if (peephIsInertLine(line))
				continue;
			if (line.opcode == MOpcode::Label) {
				crossedLabel = true;
				continue;
			}
			if (line.isLabelLike) return false;
			if (!crossedLabel ||
			    (line.opcodeText != "mov" && line.opcodeText != "fmov") ||
			    line.rawOperands.size() != 2 || line.rawOperands[1] != tempReg)
				return false;
			returnMoveBlock = b;
			returnMoveInstr = i;
			foundReturnMove = true;
			break;
		}
	}
	if (!foundReturnMove) return false;

	const MachineInstr &returnMove =
		func.blocks[returnMoveBlock].instrs[returnMoveInstr];
	if (returnMove.opcodeText != copy.opcodeText) return false;
	if (peephRegClass(returnMove.rawOperands[0]) != peephRegClass(tempReg)) return false;

	bool foundRet = false;
	for (size_t b = returnMoveBlock; b < func.blocks.size() && !foundRet; ++b) {
		const auto &instrs = func.blocks[b].instrs;
		size_t begin = (b == returnMoveBlock) ? returnMoveInstr + 1 : 0;
		for (size_t i = begin; i < instrs.size(); ++i) {
			const MachineInstr &line = instrs[i];
			if (peephIsInertLine(line))
				continue;
			if (line.isLabelLike || line.opcodeText != "ret")
				return false;
			foundRet = true;
			break;
		}
	}
	if (!foundRet) return false;

	peephReplaceInstr(block.instrs[instrIdx],
	                    peephMakeInsn(copy.opcodeText,
	                                    {returnMove.rawOperands[0], srcReg}));
	MachineInstr ret = parseMachineInstr("\tret", block.instrs[instrIdx].originalIndex);
	block.instrs.insert(block.instrs.begin() + instrIdx + 1, std::move(ret));
	return true;
}

static bool tryMachineSiblingTailCall(MachineFunction &func, size_t blockIdx,
                                      size_t instrIdx) {
	if (blockIdx >= func.blocks.size())
		return false;
	auto &block = func.blocks[blockIdx];
	if (instrIdx >= block.instrs.size())
		return false;

	std::string callee;
	if (!isDirectCall(block.instrs[instrIdx], callee))
		return false;

	std::vector<MachineInstr> epilogueBody;
	if (!collectEpilogueBody(func, epilogueBody))
		return false;

	size_t eraseEnd = instrIdx + 1;
	if (eraseEnd >= block.instrs.size())
		return false;

	std::string branchTarget;
	if (isUncondBranchTo(block.instrs[eraseEnd], branchTarget)) {
		if (branchTarget != ".L" + func.name + "_epilogue")
			return false;
		++eraseEnd;
	} else {
		const MachineInstr &saveMove = block.instrs[eraseEnd];
		if ((saveMove.opcodeText != "mov" && saveMove.opcodeText != "fmov") ||
		    saveMove.rawOperands.size() != 2 ||
		    !peephRegClass(saveMove.rawOperands[0]) ||
		    !isReturnReg(saveMove.rawOperands[1]))
			return false;
		const std::string tempReg = saveMove.rawOperands[0];
		if (eraseEnd + 1 >= block.instrs.size() ||
		    !isUncondBranchTo(block.instrs[eraseEnd + 1], branchTarget) ||
		    !matchReturnForwarder(func, branchTarget, tempReg))
			return false;
		eraseEnd += 2;
	}

	std::vector<MachineInstr> replacement;
	replacement.reserve(epilogueBody.size() + 1);
	for (auto inst : epilogueBody) {
		inst.originalIndex = block.instrs[instrIdx].originalIndex;
		replacement.push_back(std::move(inst));
	}
	replacement.push_back(parseMachineInstr("\tb " + callee,
	                                       block.instrs[instrIdx].originalIndex));

	block.instrs.erase(block.instrs.begin() + instrIdx,
	                   block.instrs.begin() + eraseEnd);
	block.instrs.insert(block.instrs.begin() + instrIdx,
	                    replacement.begin(), replacement.end());
	return true;
}

bool runMachineBranchOptimization(MachineFunction &func) {
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		for (size_t i = 0; i < func.blocks[b].instrs.size(); ++i) {
			if (tryMachineFoldCopyIntoReturn(func, b, i) ||
			    tryMachineSiblingTailCall(func, b, i) ||
			    tryMachineFallthroughBranch(func, b, i) ||
			    tryMachineBranchThreading(func, b, i) ||
			    tryMachineRemoveDeadForwarder(func, b, i))
				return true;
		}
	}

	MachineLivenessResult liveness = MachineLiveness().analyze(func);
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		for (size_t i = 0; i < func.blocks[b].instrs.size(); ++i) {
			if (tryMachineAndTBZ(func.blocks[b], i, liveness))
				return true;
		}
	}
	return false;
}
