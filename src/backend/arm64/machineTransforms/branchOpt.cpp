#include "../../../include/backend/arm64/machineTransforms/transforms.hpp"
#include "../../../include/backend/arm64/machineTransforms/utils.hpp"
#include <algorithm>
#include <climits>
#include <cstdlib>
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
	if (andLine.asmOperands.size() != 3) return false;
	if (andLine.asmOperands[2] != "#1") return false;

	const std::string &tempReg = andLine.asmOperands[0];
	const std::string &srcReg  = andLine.asmOperands[1];
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
		bool isCbz  = (line.opcodeText == "cbz"  && line.asmOperands.size() >= 2 &&
		               line.asmOperands[0] == tempReg);
		bool isCbnz = (line.opcodeText == "cbnz" && line.asmOperands.size() >= 2 &&
		               line.asmOperands[0] == tempReg);

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

			const std::string &label = line.asmOperands[1];
			std::string newMnemonic = isCbz ? "tbz" : "tbnz";
			peephReplaceInstr(block.instrs[i],
			                    peephMakeInsn(newMnemonic,
			                                    {srcReg, "#0", label}));
			block.instrs.erase(block.instrs.begin() + idx);
			return true;
		}

		// ── tst wN, wN  or  cmp wN, #0 → b.eq / b.ne ─────────
		bool isTst = (line.opcodeText == "tst" && line.asmOperands.size() >= 2 &&
		              line.asmOperands[0] == tempReg && line.asmOperands[1] == tempReg);
		bool isCmpZero = (line.opcodeText == "cmp" && line.asmOperands.size() >= 2 &&
		                  line.asmOperands[0] == tempReg &&
		                  line.asmOperands[1] == "#0");

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
				brLine.asmOperands.size() >= 1;
			if (isSupportedBranch) {
				auto liveIt = liveness.instrLiveOut.find(&block.instrs[branchIdx]);
				if (liveIt == liveness.instrLiveOut.end() ||
				    liveIt->second.count(kMachineFlagsReg))
					return false;
			}

			if (brLine.opcodeText == "b.eq" && brLine.asmOperands.size() >= 1) {
				peephReplaceInstr(block.instrs[branchIdx],
				                    peephMakeInsn("tbz",
				                                    {srcReg, "#0", brLine.asmOperands[0]}));
				block.instrs.erase(block.instrs.begin() + i);   // remove tst/cmp
				block.instrs.erase(block.instrs.begin() + idx); // remove and
				return true;
			}
			if (brLine.opcodeText == "b.ne" && brLine.asmOperands.size() >= 1) {
				peephReplaceInstr(block.instrs[branchIdx],
				                    peephMakeInsn("tbnz",
				                                    {srcReg, "#0", brLine.asmOperands[0]}));
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
	if (inst.opcodeText != "b" || inst.asmOperands.size() != 1)
		return false;
	target = inst.asmOperands[0];
	return true;
}

static bool isDirectCall(const MachineInstr &inst, std::string &target) {
	if (inst.opcodeText != "bl" || inst.asmOperands.size() != 1)
		return false;
	target = inst.asmOperands[0];
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
	    move.asmOperands.size() != 2 ||
	    !isReturnReg(move.asmOperands[0]) ||
	    !peephSamePhysicalReg(move.asmOperands[1], tempReg))
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
	if (m == "b" && line.asmOperands.size() == 1) return 0;
	if (m.size() > 2 && m.compare(0, 2, "b.") == 0 && line.asmOperands.size() == 1)
		return 0;
	if ((m == "cbz" || m == "cbnz") && line.asmOperands.size() == 2) return 1;
	if ((m == "tbz" || m == "tbnz") && line.asmOperands.size() == 3) return 2;
	return -1;
}

static std::vector<size_t> executableInstructions(const MachineBasicBlock &block) {
	std::vector<size_t> result;
	for (size_t i = 0; i < block.instrs.size(); ++i) {
		const MachineInstr &inst = block.instrs[i];
		if (!inst.isLabelLike)
			result.push_back(i);
	}
	return result;
}

static int nextExecutableBlock(const MachineFunction &func, size_t blockIdx) {
	for (size_t i = blockIdx + 1; i < func.blocks.size(); ++i)
		if (!executableInstructions(func.blocks[i]).empty())
			return static_cast<int>(i);
	return -1;
}

static int previousExecutableBlock(const MachineFunction &func, size_t blockIdx) {
	for (size_t i = blockIdx; i-- > 0;)
		if (!executableInstructions(func.blocks[i]).empty())
			return static_cast<int>(i);
	return -1;
}

static bool blockFallsThrough(const MachineBasicBlock &block) {
	auto exec = executableInstructions(block);
	if (exec.empty())
		return true;
	const MachineInstr &last = block.instrs[exec.back()];
	return !(last.opcodeText == "b" || last.opcodeText == "ret" ||
	         last.opcodeText == "br");
}

static bool parseImmediate(const std::string &text, int &value);

// Specialize the fallthrough edge of a monotone counted loop around a shared
// first-iteration test.  After register allocation this commonly looks like:
//
//   latch:   cmp  wi, bound
//            b.ge exit
//            add  wi, wi, #1
//   header:  cmp  wi, #init
//            b.eq first_only
//            ... steady path ...
//
// If wi has exactly one constant initialization and one guarded unit update,
// the latch edge can never take the equality branch.  Clone the short steady
// path onto that fallthrough edge.  This is edge specialization (jump
// threading), not a branch-probability guess: the signed guard proves the add
// cannot overflow and the induction proof holds for every input.
static bool tryMachineThreadMonotoneFirstIteration(MachineFunction &func) {
	for (size_t headerIdx = 0; headerIdx < func.blocks.size(); ++headerIdx) {
		auto headerExec = executableInstructions(func.blocks[headerIdx]);
		if (headerExec.size() < 3)
			continue;
		const MachineInstr &headerCmp =
			func.blocks[headerIdx].instrs[headerExec[0]];
		const MachineInstr &firstBranch =
			func.blocks[headerIdx].instrs[headerExec[1]];
		if (headerCmp.opcodeText != "cmp" || headerCmp.asmOperands.size() != 2 ||
		    firstBranch.opcodeText != "b.eq" ||
		    firstBranch.asmOperands.size() != 1)
			continue;

		const std::string ivReg = headerCmp.asmOperands[0];
		int init = 0;
		if (peephRegClass(ivReg) != 'w' ||
		    !parseImmediate(headerCmp.asmOperands[1], init))
			continue;

		int predIdx = previousExecutableBlock(func, headerIdx);
		if (predIdx < 0 || !blockFallsThrough(func.blocks[predIdx]))
			continue;
		auto predExec = executableInstructions(func.blocks[predIdx]);
		if (predExec.size() < 3)
			continue;
		const MachineInstr &update =
			func.blocks[predIdx].instrs[predExec.back()];
		if (update.opcodeText != "add" || update.asmOperands.size() != 3 ||
		    !peephSamePhysicalReg(update.asmOperands[0], ivReg) ||
		    !peephSamePhysicalReg(update.asmOperands[1], ivReg) ||
		    update.asmOperands[2] != "#1")
			continue;

		// The update must be control-dependent on a signed upper-bound test of
		// the old IV.  No intervening flag definition may separate cmp/b.ge.
		bool guarded = false;
		for (size_t p = predExec.size() - 1; p-- > 0;) {
			const MachineInstr &candidate =
				func.blocks[predIdx].instrs[predExec[p]];
			if (candidate.opcodeText != "b.ge")
				continue;
			for (size_t q = p; q-- > 0;) {
				const MachineInstr &cmp =
					func.blocks[predIdx].instrs[predExec[q]];
				if (!cmp.setsFlags)
					continue;
				guarded = cmp.opcodeText == "cmp" &&
				          cmp.asmOperands.size() == 2 &&
				          peephSamePhysicalReg(cmp.asmOperands[0], ivReg);
				break;
			}
			break;
		}
		if (!guarded)
			continue;

		// Whole-function def audit establishes the induction recurrence.  Be
		// deliberately strict: copies, calls defining the register, or another
		// update make the proof inapplicable.
		int initializations = 0;
		int updates = 0;
		bool unsupportedDef = false;
		for (const auto &block : func.blocks) {
			for (const auto &inst : block.instrs) {
				if (!peephLineWritesReg(inst, ivReg))
					continue;
				int imm = 0;
				bool isInit = inst.opcodeText == "mov" &&
				              inst.asmOperands.size() == 2 &&
				              peephSamePhysicalReg(inst.asmOperands[0], ivReg) &&
				              parseImmediate(inst.asmOperands[1], imm) && imm == init;
				bool isUpdate = &inst == &update;
				if (isInit) ++initializations;
				else if (isUpdate) ++updates;
				else unsupportedDef = true;
			}
		}
		if (unsupportedDef || initializations != 1 || updates != 1)
			continue;

		// The false path must be a small, closed straight-line tail.  Requiring
		// its last instruction to transfer control prevents the clone from
		// falling back into the original header.
		std::vector<MachineInstr> steadyTail;
		for (size_t i = 2; i < headerExec.size(); ++i)
			steadyTail.push_back(func.blocks[headerIdx].instrs[headerExec[i]]);
		if (steadyTail.empty() ||
		    !peephIsControlFlowBarrier(steadyTail.back()) ||
		    steadyTail.size() > 6)
			continue;

		auto &predInstrs = func.blocks[predIdx].instrs;
		int originalIndex = update.originalIndex;
		for (auto &inst : steadyTail) {
			inst.originalIndex = originalIndex;
			predInstrs.push_back(std::move(inst));
		}
		return true;
	}
	return false;
}

static std::string normalizedIntegerReg(const std::string &reg) {
	char cls = 0;
	std::string number;
	if (!peephParsePhysicalReg(reg, cls, number) ||
	    (cls != 'w' && cls != 'x'))
		return "";
	return "r" + number;
}

static bool parseImmediate(const std::string &text, int &value) {
	if (text.size() < 2 || text[0] != '#')
		return false;
	char *end = nullptr;
	long parsed = std::strtol(text.c_str() + 1, &end, 0);
	if (!end || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
		return false;
	value = static_cast<int>(parsed);
	return true;
}

enum class LowBitParity { Even, Odd };
using ParityFacts = std::map<std::string, LowBitParity>;

static bool mergeParityFacts(ParityFacts &dst, unsigned char &initialized,
	                         const ParityFacts &src) {
	if (!initialized) {
		dst = src;
		initialized = true;
		return true;
	}
	bool changed = false;
	for (auto it = dst.begin(); it != dst.end();) {
		auto incoming = src.find(it->first);
		if (incoming == src.end() || incoming->second != it->second) {
			it = dst.erase(it);
			changed = true;
		} else {
			++it;
		}
	}
	return changed;
}

static bool operandParity(const std::string &operand,
	                      const ParityFacts &facts,
	                      LowBitParity &parity) {
	int immediate = 0;
	if (parseImmediate(operand, immediate)) {
		parity = (immediate & 1) ? LowBitParity::Odd : LowBitParity::Even;
		return true;
	}
	std::string reg = normalizedIntegerReg(operand);
	if (reg.empty()) return false;
	auto it = facts.find(reg);
	if (it == facts.end()) return false;
	parity = it->second;
	return true;
}

static void transferParity(const MachineInstr &inst, ParityFacts &facts) {
	if (inst.isCall) {
		// Calls clobber the caller-saved integer bank.  Clearing all facts is
		// conservative and still leaves loop-local induction proofs available.
		facts.clear();
		return;
	}
	if (inst.asmOperands.empty()) return;
	const std::string dst = normalizedIntegerReg(inst.asmOperands[0]);
	if (dst.empty() || !peephLineWritesReg(inst, inst.asmOperands[0]))
		return;

	bool known = false;
	LowBitParity result = LowBitParity::Even;
	if ((inst.opcodeText == "mov" || inst.opcodeText == "movz") &&
	    inst.asmOperands.size() >= 2) {
		known = operandParity(inst.asmOperands[1], facts, result);
	} else if (inst.opcodeText == "movk") {
		// movk with a non-zero shift preserves bit zero; without a shift it
		// replaces bit zero from the immediate.
		if (inst.asmOperands.size() >= 3 && inst.asmOperands[2] != "lsl #0") {
			auto old = facts.find(dst);
			if (old != facts.end()) { result = old->second; known = true; }
		} else if (inst.asmOperands.size() >= 2) {
			known = operandParity(inst.asmOperands[1], facts, result);
		}
	} else if ((inst.opcodeText == "add" || inst.opcodeText == "sub") &&
	           inst.asmOperands.size() >= 3) {
		LowBitParity lhs, rhs;
		bool lhsKnown = operandParity(inst.asmOperands[1], facts, lhs);
		bool shiftedEven = inst.asmOperands.size() >= 4 &&
		                   inst.asmOperands[3].rfind("lsl #", 0) == 0 &&
		                   inst.asmOperands[3] != "lsl #0";
		bool rhsKnown = shiftedEven ||
		                operandParity(inst.asmOperands[2], facts, rhs);
		if (shiftedEven) rhs = LowBitParity::Even;
		if (lhsKnown && rhsKnown) {
			result = lhs == rhs ? LowBitParity::Even : LowBitParity::Odd;
			known = true;
		}
	} else if (inst.opcodeText == "mul" && inst.asmOperands.size() >= 3) {
		LowBitParity lhs, rhs;
		if (operandParity(inst.asmOperands[1], facts, lhs) &&
		    operandParity(inst.asmOperands[2], facts, rhs)) {
			result = (lhs == LowBitParity::Odd && rhs == LowBitParity::Odd)
			             ? LowBitParity::Odd : LowBitParity::Even;
			known = true;
		}
	} else if (inst.opcodeText == "and" && inst.asmOperands.size() >= 3) {
		int mask = 0;
		if (parseImmediate(inst.asmOperands[2], mask)) {
			if ((mask & 1) == 0) {
				result = LowBitParity::Even;
				known = true;
			} else {
				known = operandParity(inst.asmOperands[1], facts, result);
			}
		}
	} else if (inst.opcodeText == "lsl" && inst.asmOperands.size() >= 3) {
		int shift = 0;
		if (parseImmediate(inst.asmOperands[2], shift) && shift > 0) {
			result = LowBitParity::Even;
			known = true;
		}
	}

	if (known) facts[dst] = result;
	else facts.erase(dst);
}

struct MachineParityResult {
	std::vector<ParityFacts> atTerminator;
};

static MachineParityResult analyzeMachineParity(const MachineFunction &func) {
	const size_t count = func.blocks.size();
	std::vector<ParityFacts> in(count);
	std::vector<unsigned char> initialized(count, false);
	std::vector<bool> queued(count, false);
	std::vector<size_t> worklist;
	MachineParityResult result;
	result.atTerminator.resize(count);
	if (count == 0) return result;
	initialized[0] = true;
	queued[0] = true;
	worklist.push_back(0);

	auto enqueue = [&](int target, const ParityFacts &facts) {
		if (target < 0 || static_cast<size_t>(target) >= count) return;
		if (mergeParityFacts(in[target], initialized[target], facts) &&
		    !queued[target]) {
			queued[target] = true;
			worklist.push_back(static_cast<size_t>(target));
		}
	};

	while (!worklist.empty()) {
		size_t blockIdx = worklist.back();
		worklist.pop_back();
		queued[blockIdx] = false;
		ParityFacts facts = in[blockIdx];
		bool terminated = false;
		auto exec = executableInstructions(func.blocks[blockIdx]);
		for (size_t pos : exec) {
			const MachineInstr &inst = func.blocks[blockIdx].instrs[pos];
			int targetOperand = branchTargetOperandIndex(inst);
			if (targetOperand >= 0 &&
			    static_cast<size_t>(targetOperand) < inst.asmOperands.size()) {
				int target = findBlockByLabel(func, inst.asmOperands[targetOperand]);
				ParityFacts taken = facts;
				ParityFacts fallthrough = facts;
				if ((inst.opcodeText == "tbz" || inst.opcodeText == "tbnz") &&
				    inst.asmOperands.size() == 3 && inst.asmOperands[1] == "#0") {
					std::string reg = normalizedIntegerReg(inst.asmOperands[0]);
					if (!reg.empty()) {
						bool takenEven = inst.opcodeText == "tbz";
						taken[reg] = takenEven ? LowBitParity::Even : LowBitParity::Odd;
						fallthrough[reg] = takenEven ? LowBitParity::Odd : LowBitParity::Even;
					}
				}
				enqueue(target, taken);
				if (inst.opcodeText == "b") {
					result.atTerminator[blockIdx] = facts;
					terminated = true;
					break;
				}
				facts = std::move(fallthrough);
				continue;
			}
			if (inst.opcodeText == "ret" || inst.opcodeText == "br") {
				result.atTerminator[blockIdx] = facts;
				terminated = true;
				break;
			}
			transferParity(inst, facts);
		}
		if (!terminated) {
			result.atTerminator[blockIdx] = facts;
			enqueue(nextExecutableBlock(func, blockIdx), facts);
		}
	}
	return result;
}

// Collapse a side-effect-free exact-halving control cycle:
//
//   tbz state,#0,even       even: asr state,state,#1
//   ...                     latch: cmp state,#odd_sentinel
//                                      add count,count,#1
//                                      b.eq exit
//
// into one AArch64 variable shift.  rbit+clz computes the number of trailing
// zero bits, so a run of N consecutive even iterations executes the loop
// control only once.  Other predecessors keep the unit-count latch cloned on
// their edge.  Requiring the sentinel to be odd proves that no intermediate
// exact shift can have taken the exit before the final shifted value.
static bool tryMachineBatchExactHalvingLoop(
	MachineFunction &func, const MachineLivenessResult &liveness) {
	MachineParityResult parity = analyzeMachineParity(func);
	const bool enableParityEdgeThreading =
		std::getenv("DISABLE_PARITY_EDGE_THREAD") == nullptr;
	for (size_t parityIdx = 0; parityIdx < func.blocks.size(); ++parityIdx) {
		auto parityExec = executableInstructions(func.blocks[parityIdx]);
		if (parityExec.size() != 1)
			continue;
		const MachineInstr &bitBranch =
			func.blocks[parityIdx].instrs[parityExec[0]];
		if (bitBranch.opcodeText != "tbz" || bitBranch.asmOperands.size() != 3 ||
		    bitBranch.asmOperands[1] != "#0")
			continue;
		const std::string stateReg = bitBranch.asmOperands[0];
		if (peephRegClass(stateReg) != 'w')
			continue;

		int evenIdx = findBlockByLabel(func, bitBranch.asmOperands[2]);
		if (evenIdx < 0)
			continue;
		bool evenHasOtherEntry = false;
		for (size_t b = 0; b < func.blocks.size(); ++b) {
			for (size_t pos : executableInstructions(func.blocks[b])) {
				const MachineInstr &inst = func.blocks[b].instrs[pos];
				int targetOperand = branchTargetOperandIndex(inst);
				if (targetOperand >= 0 &&
				    inst.asmOperands[targetOperand] == bitBranch.asmOperands[2] &&
				    &inst != &bitBranch) {
					evenHasOtherEntry = true;
					break;
				}
			}
			if (evenHasOtherEntry)
				break;
		}
		int beforeEven = previousExecutableBlock(func, static_cast<size_t>(evenIdx));
		if (beforeEven >= 0 && blockFallsThrough(func.blocks[beforeEven]))
			evenHasOtherEntry = true;
		if (evenHasOtherEntry)
			continue;
		auto evenExec = executableInstructions(func.blocks[evenIdx]);
		if (evenExec.size() != 1)
			continue;
		const MachineInstr &shift = func.blocks[evenIdx].instrs[evenExec[0]];
		if (shift.opcodeText != "asr" || shift.asmOperands.size() != 3 ||
		    !peephSamePhysicalReg(shift.asmOperands[0], stateReg) ||
		    !peephSamePhysicalReg(shift.asmOperands[1], stateReg) ||
		    shift.asmOperands[2] != "#1")
			continue;

		int latchIdx = nextExecutableBlock(func, static_cast<size_t>(evenIdx));
		if (latchIdx < 0)
			continue;
		auto latchExec = executableInstructions(func.blocks[latchIdx]);
		if (latchExec.size() != 3 && latchExec.size() != 4)
			continue;
		size_t comparePos = latchExec[0];
		size_t incrementPos = latchExec[1];
		if (func.blocks[latchIdx].instrs[comparePos].opcodeText == "add" &&
		    func.blocks[latchIdx].instrs[incrementPos].opcodeText == "cmp")
			std::swap(comparePos, incrementPos);
		const MachineInstr &compare =
			func.blocks[latchIdx].instrs[comparePos];
		const MachineInstr &increment =
			func.blocks[latchIdx].instrs[incrementPos];
		const MachineInstr &exitBranch =
			func.blocks[latchIdx].instrs[latchExec[2]];
		int sentinel = 0;
		if (compare.opcodeText != "cmp" || compare.asmOperands.size() != 2 ||
		    !peephSamePhysicalReg(compare.asmOperands[0], stateReg) ||
		    !parseImmediate(compare.asmOperands[1], sentinel) ||
		    (sentinel & 1) == 0 ||
		    increment.opcodeText != "add" || increment.asmOperands.size() != 3 ||
		    !peephSamePhysicalReg(increment.asmOperands[0], increment.asmOperands[1]) ||
		    increment.asmOperands[2] != "#1" ||
		    exitBranch.opcodeText != "b.eq" || exitBranch.asmOperands.size() != 1)
			continue;
		const std::string countReg = increment.asmOperands[0];
		if (peephRegClass(countReg) != 'w' ||
		    peephSamePhysicalReg(countReg, stateReg))
			continue;

		std::string parityLabel;
		if (latchExec.size() == 4) {
			const MachineInstr &continueBranch =
				func.blocks[latchIdx].instrs[latchExec[3]];
			if (continueBranch.opcodeText != "b" ||
			    continueBranch.asmOperands.size() != 1 ||
			    findBlockByLabel(func, continueBranch.asmOperands[0]) !=
			        static_cast<int>(parityIdx))
				continue;
			parityLabel = continueBranch.asmOperands[0];
		} else {
			int continueIdx =
				nextExecutableBlock(func, static_cast<size_t>(latchIdx));
			if (continueIdx != static_cast<int>(parityIdx))
				continue;
		}
		std::string latchLabel;
		for (const auto &inst : func.blocks[latchIdx].instrs) {
			if (inst.opcode == MOpcode::Label) {
				latchLabel = labelName(inst);
				break;
			}
		}
		if (parityLabel.empty()) {
			for (const auto &inst : func.blocks[parityIdx].instrs) {
				if (inst.opcode == MOpcode::Label) {
					parityLabel = labelName(inst);
					break;
				}
			}
		}
		if (latchLabel.empty() || parityLabel.empty())
			continue;

		std::vector<std::pair<size_t, size_t>> explicitPreds;
		bool unsupportedPred = false;
		for (size_t b = 0; b < func.blocks.size(); ++b) {
			auto exec = executableInstructions(func.blocks[b]);
			for (size_t pos : exec) {
				const MachineInstr &inst = func.blocks[b].instrs[pos];
				int targetOperand = branchTargetOperandIndex(inst);
				if (targetOperand < 0 ||
				    inst.asmOperands[targetOperand] != latchLabel)
					continue;
				if (inst.opcodeText != "b" || pos != exec.back() ||
				    static_cast<int>(b) == evenIdx) {
					unsupportedPred = true;
					break;
				}
				explicitPreds.push_back({b, pos});
			}
			if (unsupportedPred)
				break;
		}
		if (unsupportedPred)
			continue;

		std::set<std::string> unavailable;
		unavailable.insert(normalizedIntegerReg(stateReg));
		unavailable.insert(normalizedIntegerReg(countReg));
		auto collectLive = [&](size_t b) {
			if (b < liveness.blockLiveIn.size())
				unavailable.insert(liveness.blockLiveIn[b].begin(),
				                   liveness.blockLiveIn[b].end());
			if (b < liveness.blockLiveOut.size())
				unavailable.insert(liveness.blockLiveOut[b].begin(),
				                   liveness.blockLiveOut[b].end());
		};
		collectLive(static_cast<size_t>(evenIdx));
		collectLive(static_cast<size_t>(latchIdx));
		std::string scratchReg;
		for (int reg = 9; reg <= 17; ++reg) {
			if (!unavailable.count("r" + std::to_string(reg))) {
				scratchReg = "w" + std::to_string(reg);
				break;
			}
		}
		if (scratchReg.empty())
			continue;

		// Clone the original unit-count latch onto every non-even edge before
		// specializing the shared fallthrough latch for the batched even edge.
		for (auto [predIdx, branchPos] : explicitPreds) {
			auto &instructions = func.blocks[predIdx].instrs;
			int originalIndex = instructions[branchPos].originalIndex;
			std::vector<MachineInstr> replacement;
			bool cannotEqualSentinel = false;
			if (enableParityEdgeThreading &&
			    predIdx < parity.atTerminator.size()) {
				auto fact = parity.atTerminator[predIdx].find(
					normalizedIntegerReg(stateReg));
				if (fact != parity.atTerminator[predIdx].end()) {
					LowBitParity sentinelParity = (sentinel & 1)
						? LowBitParity::Odd : LowBitParity::Even;
					cannotEqualSentinel = fact->second != sentinelParity;
				}
			}
			if (!cannotEqualSentinel)
				replacement.push_back(compare);
			replacement.push_back(increment);
			if (!cannotEqualSentinel)
				replacement.push_back(exitBranch);
			replacement.push_back(parseMachineInstr(
				"\tb " + parityLabel, originalIndex));
			for (auto &inst : replacement)
				inst.originalIndex = originalIndex;
			instructions.erase(instructions.begin() + branchPos);
			instructions.insert(instructions.begin() + branchPos,
			                    replacement.begin(), replacement.end());
		}

		auto &evenInstructions = func.blocks[evenIdx].instrs;
		size_t shiftPos = evenExec[0];
		int originalIndex = evenInstructions[shiftPos].originalIndex;
		MachineInstr reverse = parseMachineInstr(
			peephMakeInsn("rbit", {scratchReg, stateReg}), originalIndex);
		MachineInstr leadingZeros = parseMachineInstr(
			peephMakeInsn("clz", {scratchReg, scratchReg}), originalIndex);
		evenInstructions.insert(evenInstructions.begin() + shiftPos,
		                        std::move(reverse));
		evenInstructions.insert(evenInstructions.begin() + shiftPos + 1,
		                        std::move(leadingZeros));
		peephReplaceInstr(evenInstructions[shiftPos + 2],
		                    peephMakeInsn("asr", {stateReg, stateReg, scratchReg}));

		auto &latchInstructions = func.blocks[latchIdx].instrs;
		peephReplaceInstr(latchInstructions[incrementPos],
		                    peephMakeInsn("add", {countReg, countReg, scratchReg}));
		return true;
	}
	return false;
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
		if (line.opcodeText != "b" || line.asmOperands.size() != 1)
			return target;
		target = line.asmOperands[0];
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
	const std::string target = line.asmOperands[ti];
	std::string finalTarget = resolveForwardTarget(func, target);
	if (finalTarget.empty() || finalTarget == target) return false;

	auto newBranchOps = line.asmOperands;
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
			for (const auto &op : l.asmOperands)
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
				    ((l.opcodeText == "b" && l.asmOperands.size() == 1) ||
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
			    l.asmOperands.size() == 1) {
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
	if (line.opcodeText != "b" || line.asmOperands.size() != 1) return false;
	if (!nextVisibleIsLabel(func, blockIdx, instrIdx, line.asmOperands[0])) return false;

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
	if (copy.asmOperands.size() != 2) return false;
	const std::string tempReg = copy.asmOperands[0];
	const std::string srcReg = copy.asmOperands[1];
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
			    line.asmOperands.size() != 2 || line.asmOperands[1] != tempReg)
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
	if (peephRegClass(returnMove.asmOperands[0]) != peephRegClass(tempReg)) return false;

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
	                                    {returnMove.asmOperands[0], srcReg}));
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
		    saveMove.asmOperands.size() != 2 ||
		    !peephRegClass(saveMove.asmOperands[0]) ||
		    !isReturnReg(saveMove.asmOperands[1]))
			return false;
		const std::string tempReg = saveMove.asmOperands[0];
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

bool runMachineCFGOptimization(MachineFunction &func) {
	if (!std::getenv("DISABLE_MONOTONE_FIRST_ITER_THREAD") &&
	    tryMachineThreadMonotoneFirstIteration(func))
		return true;
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

	return false;
}

bool runMachineBitBranchOptimization(
    MachineFunction &func, const MachineLivenessResult &liveness) {
	if (tryMachineBatchExactHalvingLoop(func, liveness))
		return true;
	for (size_t b = 0; b < func.blocks.size(); ++b) {
		for (size_t i = 0; i < func.blocks[b].instrs.size(); ++i) {
			if (tryMachineAndTBZ(func.blocks[b], i, liveness))
				return true;
		}
	}
	return false;
}
