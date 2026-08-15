// Control-flow cleanup, block layout, and branch relaxation operate on the
// final machine CFG independently from local instruction optimizations.
#include "backend/cfg_optimizations.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {

namespace {

struct ForwardTraceScore {
	unsigned deepestLoop = 0;
	unsigned weightedLength = 0;
};

struct BlockPlacementContext {
	std::vector<MachineBasicBlock *> &order;
	std::unordered_set<MachineBasicBlock *> &placed;

	ForwardTraceScore scoreForwardTrace(
	    MachineBasicBlock *current, MachineBasicBlock *traceStart,
	    std::unordered_map<MachineBasicBlock *, ForwardTraceScore> &memo,
	    std::unordered_set<MachineBasicBlock *> &active,
	    MachineBasicBlock *block) {
		if (!block || block == current)
			return ForwardTraceScore();
		if (block != traceStart &&
		    (placed.count(block) || block->number() <= current->number()))
			return ForwardTraceScore();
		auto found = memo.find(block);
		if (found != memo.end())
			return found->second;
		if (!active.insert(block).second)
			return ForwardTraceScore();
		if (memo.size() + active.size() > 64) {
			active.erase(block);
			return ForwardTraceScore{
			    block->loopDepth,
			    1 + 4 * std::min(block->loopDepth, 4U)};
		}

		ForwardTraceScore bestChild;
		for (MachineBasicBlock *successor : block->successors()) {
			ForwardTraceScore child = scoreForwardTrace(
			    current, traceStart, memo, active, successor);
			if (child.deepestLoop > bestChild.deepestLoop ||
			    (child.deepestLoop == bestChild.deepestLoop &&
			     child.weightedLength > bestChild.weightedLength))
				bestChild = child;
		}
		active.erase(block);
		ForwardTraceScore result;
		result.deepestLoop = std::max(block->loopDepth, bestChild.deepestLoop);
		result.weightedLength =
		    1 + 4 * std::min(block->loopDepth, 4U) + bestChild.weightedLength;
		memo.emplace(block, result);
		return result;
	}

	void extendChain(MachineBasicBlock *start) {
		MachineBasicBlock *current = start;
		while (current && placed.insert(current).second) {
			order.push_back(current);
			MachineBasicBlock *preferredFallthrough = nullptr;
			MachineBasicBlock *likelySuccessor = nullptr;
			unsigned deepestSuccessor = 0;
			bool depthsDiffer = false;
			if (!current->successors().empty()) {
				deepestSuccessor = current->successors().front()->loopDepth;
				unsigned shallowestSuccessor = deepestSuccessor;
				for (MachineBasicBlock *successor : current->successors()) {
					deepestSuccessor =
					    std::max(deepestSuccessor, successor->loopDepth);
					shallowestSuccessor =
					    std::min(shallowestSuccessor, successor->loopDepth);
				}
				depthsDiffer = deepestSuccessor != shallowestSuccessor;
			}
			if (!depthsDiffer && current->instructions().size() >= 2) {
				auto unconditional = std::prev(current->instructions().end());
				auto conditional = std::prev(unconditional);
				bool hasConditional = conditional->opcode() == Opcode::Bcc ||
				                      conditional->opcode() == Opcode::CBZ ||
				                      conditional->opcode() == Opcode::CBNZ;
				if (unconditional->opcode() == Opcode::B && hasConditional &&
				    unconditional->operands().size() == 1 &&
				    unconditional->operands()[0].kind() ==
				        MachineOperand::Kind::BasicBlock) {
					preferredFallthrough =
					    unconditional->operands()[0].basicBlock();
					if (conditional->opcode() == Opcode::Bcc &&
					    conditional->operands().size() >= 2 &&
					    conditional->operands()[0].kind() ==
					        MachineOperand::Kind::ConditionCode &&
					    conditional->operands()[1].kind() ==
					        MachineOperand::Kind::BasicBlock) {
						MachineBasicBlock *conditionalTarget =
						    conditional->operands()[1].basicBlock();
						CondCode condition =
						    conditional->operands()[0].condition();
						if (condition == CondCode::EQ)
							likelySuccessor = preferredFallthrough;
						else if (condition == CondCode::NE)
							likelySuccessor = conditionalTarget;
					} else if ((conditional->opcode() == Opcode::CBZ ||
					            conditional->opcode() == Opcode::CBNZ) &&
					           conditional->operands().size() >= 2 &&
					           conditional->operands()[1].kind() ==
					               MachineOperand::Kind::BasicBlock) {
						MachineBasicBlock *conditionalTarget =
						    conditional->operands()[1].basicBlock();
						likelySuccessor = conditional->opcode() == Opcode::CBZ
						                      ? preferredFallthrough
						                      : conditionalTarget;
					}
				}
			}

			MachineBasicBlock *best = nullptr;
			int bestScore = -1;
			for (unsigned i = 0; i < current->successors().size(); ++i) {
				MachineBasicBlock *successor = current->successors()[i];
				if (placed.count(successor))
					continue;
				int score = successor->predecessors().size() == 1 ? 100 : 0;
				if (depthsDiffer && successor->loopDepth == deepestSuccessor)
					score += 300;
				else if (!depthsDiffer) {
					std::unordered_map<MachineBasicBlock *, ForwardTraceScore> memo;
					std::unordered_set<MachineBasicBlock *> active;
					ForwardTraceScore trace = scoreForwardTrace(
					    current, successor, memo, active, successor);
					score += static_cast<int>(
					    std::min(trace.deepestLoop, 8U) * 100000U);
					if (successor == likelySuccessor)
						score += 10000;
					score += static_cast<int>(
					    std::min(trace.weightedLength, 999U) * 10U);
					if (successor == preferredFallthrough)
						++score;
				} else if (!preferredFallthrough && i == 0)
					score += 10;
				score += successor->number() > current->number() ? 1 : 0;
				if (score > bestScore) {
					bestScore = score;
					best = successor;
				}
			}
			current = best;
		}
	}
};

bool hasExplicitTransfer(MachineBasicBlock *block) {
	return !block->instructions().empty() &&
	       block->instructions().back().isTerminator();
}

MachineBasicBlock *targetOfConditional(const MachineInstr &instruction) {
	switch (instruction.opcode()) {
	case Opcode::Bcc:
	case Opcode::CBZ:
	case Opcode::CBNZ:
		if (instruction.operands().size() >= 2 &&
		    instruction.operands()[1].kind() ==
		        MachineOperand::Kind::BasicBlock)
			return instruction.operands()[1].basicBlock();
		break;
	case Opcode::TBZ:
	case Opcode::TBNZ:
		if (instruction.operands().size() >= 3 &&
		    instruction.operands()[2].kind() ==
		        MachineOperand::Kind::BasicBlock)
			return instruction.operands()[2].basicBlock();
		break;
	default:
		break;
	}
	return nullptr;
}

bool invertConditional(MachineInstr &instruction, MachineBasicBlock *target) {
	switch (instruction.opcode()) {
	case Opcode::Bcc:
		instruction.operands()[0] = MachineOperand::condition(
		    InstrInfo::inverseCondition(
		        instruction.operands()[0].condition()));
		instruction.operands()[1] = MachineOperand::block(target);
		return true;
	case Opcode::CBZ:
	case Opcode::CBNZ:
		instruction.setOpcode(instruction.opcode() == Opcode::CBZ ? Opcode::CBNZ
		                                                          : Opcode::CBZ);
		instruction.operands()[1] = MachineOperand::block(target);
		return true;
	case Opcode::TBZ:
	case Opcode::TBNZ:
		instruction.setOpcode(instruction.opcode() == Opcode::TBZ ? Opcode::TBNZ
		                                                          : Opcode::TBZ);
		instruction.operands()[2] = MachineOperand::block(target);
		return true;
	default:
		return false;
	}
}

std::int64_t emittedInstructionSize(const MachineInstr &instruction) {
	if (instruction.opcode() == Opcode::COPYXtoW &&
	    instruction.operands().size() >= 2 &&
	    instruction.operands()[0].isPhysicalRegister() &&
	    instruction.operands()[1].isPhysicalRegister() &&
	    RegisterInfo::aliases(instruction.operands()[0].physicalRegister(),
                          instruction.operands()[1].physicalRegister()))
		return 0;
	return 4;
}

unsigned branchTargetOperand(Opcode opcode) {
	return opcode == Opcode::TBZ || opcode == Opcode::TBNZ ? 2 : 1;
}

bool branchDisplacementFits(Opcode opcode, std::int64_t displacement) {
	const std::int64_t range =
	    opcode == Opcode::TBZ || opcode == Opcode::TBNZ
	        ? (std::int64_t{1} << 15)
	        : (std::int64_t{1} << 20);
	return displacement >= -range && displacement <= range - 4 &&
	       displacement % 4 == 0;
}

} // namespace

bool UnreachableMachineBlockElimination::run(MachineFunction &function) {
	auto &blocks = function.blocks();
	if (blocks.size() < 2)
		return false;

	std::unordered_set<MachineBasicBlock *> reachable;
	std::vector<MachineBasicBlock *> worklist;
	if (MachineBasicBlock *entry = function.entryBlock()) {
		reachable.insert(entry);
		worklist.push_back(entry);
	}
	while (!worklist.empty()) {
		MachineBasicBlock *block = worklist.back();
		worklist.pop_back();
		for (MachineBasicBlock *successor : block->successors()) {
			if (successor && reachable.insert(successor).second)
				worklist.push_back(successor);
		}
	}

	if (reachable.size() == blocks.size())
		return false;

	std::unordered_set<VReg> retainedVRegs;
	for (const auto &owned : blocks) {
		if (!reachable.count(owned.get()))
			continue;
		for (const MachineInstr &instruction : owned->instructions())
			for (const MachineOperand &operand : instruction.operands())
				if (operand.isVirtualRegister())
					retainedVRegs.insert(operand.virtualRegister());
	}

	for (auto blockIt = blocks.begin(); blockIt != blocks.end();) {
		MachineBasicBlock *block = blockIt->get();
		if (reachable.count(block)) {
			++blockIt;
			continue;
		}
		std::vector<MachineBasicBlock *> predecessors = block->predecessors();
		std::vector<MachineBasicBlock *> successors = block->successors();
		for (MachineBasicBlock *predecessor : predecessors)
			predecessor->removeSuccessor(block);
		for (MachineBasicBlock *successor : successors)
			block->removeSuccessor(successor);
		blockIt = blocks.erase(blockIt);
	}

	std::vector<VReg> deadVRegs;
	for (const auto &[reg, info] : function.registerInfo().virtualRegisters()) {
		(void)info;
		if (!retainedVRegs.count(reg))
			deadVRegs.push_back(reg);
	}
	for (VReg reg : deadVRegs)
		function.registerInfo().eraseVirtualRegister(reg);

	function.clearProperty(MachineProperty::TracksLiveness);
	return true;
}

bool MachineBlockPlacement::run(MachineFunction &function) {
	auto &blocks = function.blocks();
	if (blocks.size() < 2)
		return false;

	bool changed = false;

	// Allocation and copy propagation often turn split PHI edges into a
	// single unconditional branch.  Thread all predecessors through such
	// forwarding blocks before choosing layout; otherwise hot paths pay for
	// artificial edge blocks that no longer carry copies.
	bool threaded = true;
	while (threaded) {
		threaded = false;
		for (auto blockIt = std::next(blocks.begin()); blockIt != blocks.end();
		     ++blockIt) {
			MachineBasicBlock *forwarder = blockIt->get();
			if (forwarder->instructions().size() != 1)
				continue;
			const MachineInstr &branch = forwarder->instructions().front();
			if (branch.opcode() != Opcode::B || branch.operands().size() != 1 ||
			    branch.operands()[0].kind() != MachineOperand::Kind::BasicBlock)
				continue;
			MachineBasicBlock *target = branch.operands()[0].basicBlock();
			if (!target || target == forwarder)
				continue;

			std::vector<MachineBasicBlock *> predecessors =
			    forwarder->predecessors();
			for (MachineBasicBlock *predecessor : predecessors) {
				for (MachineInstr &instruction : predecessor->instructions())
					for (MachineOperand &operand : instruction.operands())
						if (operand.kind() ==
						        MachineOperand::Kind::BasicBlock &&
						    operand.basicBlock() == forwarder)
							operand = MachineOperand::block(target);
				predecessor->removeSuccessor(forwarder);
				predecessor->addSuccessor(target);
			}
			forwarder->removeSuccessor(target);
			blocks.erase(blockIt);
			changed = true;
			threaded = true;
			break;
		}
	}

	std::vector<MachineBasicBlock *> order;
	std::unordered_set<MachineBasicBlock *> placed;
	BlockPlacementContext placement{order, placed};
	placement.extendChain(blocks.front().get());
	for (const auto &block : blocks)
		if (!placed.count(block.get()))
			placement.extendChain(block.get());

	// Rotate a conditional loop latch immediately before its header when
	// every displaced CFG edge is explicit.  The final branch cleanup can
	// then invert the exit condition and make the hot backedge fall through,
	// matching LLVM's MachineBlockPlacement loop rotation.  Block numbers
	// are used only as the construction-order indication of a backedge; no
	// block name or source-level pattern participates in the decision.
	for (const auto &owned : blocks) {
		MachineBasicBlock *latch = owned.get();
		if (latch == function.entryBlock() || latch->successors().size() != 2 ||
		    latch->loopDepth == 0)
			continue;
		MachineBasicBlock *header = nullptr;
		for (MachineBasicBlock *successor : latch->successors())
			if (successor->loopDepth > 0 &&
			    successor->number() < latch->number()) {
				header = successor;
				break;
			}
		if (!header || header == function.entryBlock())
			continue;

		bool safe = true;
		for (MachineBasicBlock *predecessor : header->predecessors())
			if (predecessor != latch && !hasExplicitTransfer(predecessor))
				safe = false;
		for (MachineBasicBlock *predecessor : latch->predecessors())
			if (!hasExplicitTransfer(predecessor))
				safe = false;
		if (!safe)
			continue;

		auto latchPosition = std::find(order.begin(), order.end(), latch);
		auto headerPosition = std::find(order.begin(), order.end(), header);
		if (latchPosition == order.end() || headerPosition == order.end() ||
		    std::next(latchPosition) == headerPosition)
			continue;
		order.erase(latchPosition);
		headerPosition = std::find(order.begin(), order.end(), header);
		order.insert(headerPosition, latch);
	}

	for (unsigned i = 0; i < order.size(); ++i)
		changed |= order[i] != blocks[i].get();
	if (changed) {
		std::unordered_map<MachineBasicBlock *, std::size_t> index;
		for (std::size_t i = 0; i < blocks.size(); ++i)
			index[blocks[i].get()] = i;
		std::vector<std::unique_ptr<MachineBasicBlock>> reordered;
		reordered.reserve(blocks.size());
		for (MachineBasicBlock *block : order)
			reordered.push_back(std::move(blocks[index.at(block)]));
		blocks = std::move(reordered);
	}

	for (std::size_t i = 0; i < blocks.size(); ++i) {
		MachineBasicBlock *fallthrough =
		    i + 1 < blocks.size() ? blocks[i + 1].get() : nullptr;
		auto &instructions = blocks[i]->instructions();
		auto &successors = blocks[i]->successors();
		if (instructions.empty() || !instructions.back().isTerminator()) {
			if (successors.size() == 1 && successors.front() != fallthrough) {
				MachineInstr branch(Opcode::B);
				branch.addOperand(MachineOperand::block(successors.front()));
				instructions.push_back(std::move(branch));
				changed = true;
			}
			continue;
		}

		// If both explicit branch arms name the same block, retain only the
		// unconditional transfer.  This can appear after forwarding-block
		// threading has made two formerly distinct edges equivalent.
		if (instructions.size() >= 2 &&
		    instructions.back().opcode() == Opcode::B) {
			auto unconditional = std::prev(instructions.end());
			auto conditional = std::prev(unconditional);
			MachineBasicBlock *fallback =
			    unconditional->operands().empty()
			        ? nullptr
			        : unconditional->operands()[0].basicBlock();
			if (fallback && targetOfConditional(*conditional) == fallback) {
				instructions.erase(conditional);
				changed = true;
			}
		}

		// A lone conditional encodes its other CFG edge as an implicit
		// fallthrough.  Rebuild that edge after layout changes.
		if (!instructions.empty()) {
			auto last = std::prev(instructions.end());
			MachineBasicBlock *conditionalTarget = targetOfConditional(*last);
			if (conditionalTarget) {
				if (successors.size() == 1 &&
				    successors.front() == conditionalTarget) {
					if (conditionalTarget == fallthrough) {
						instructions.erase(last);
					} else {
						MachineInstr branch(Opcode::B);
						branch.addOperand(
						    MachineOperand::block(conditionalTarget));
						*last = std::move(branch);
					}
					changed = true;
					continue;
				}

				if (successors.size() == 2) {
					auto targetPosition =
					    std::find(successors.begin(), successors.end(),
					              conditionalTarget);
					if (targetPosition != successors.end()) {
						MachineBasicBlock *other =
						    successors[targetPosition == successors.begin() ? 1 : 0];
						if (fallthrough == conditionalTarget) {
							changed |= invertConditional(*last, other);
						} else if (fallthrough != other) {
							MachineInstr branch(Opcode::B);
							branch.addOperand(MachineOperand::block(other));
							instructions.push_back(std::move(branch));
							changed = true;
						}
						continue;
					}
				}
			}
		}

		auto unconditional = std::prev(instructions.end());
		if (unconditional->opcode() != Opcode::B ||
		    unconditional->operands().empty())
			continue;
		MachineBasicBlock *fallback = unconditional->operands()[0].basicBlock();
		if (fallback == fallthrough) {
			instructions.erase(unconditional);
			changed = true;
			continue;
		}
		if (unconditional == instructions.begin())
			continue;
		auto conditional = std::prev(unconditional);
		MachineBasicBlock *conditionalTarget = nullptr;
		if (conditional->opcode() == Opcode::Bcc &&
		    conditional->operands().size() >= 2)
			conditionalTarget = conditional->operands()[1].basicBlock();
		else if ((conditional->opcode() == Opcode::CBZ ||
		          conditional->opcode() == Opcode::CBNZ) &&
		         conditional->operands().size() >= 2)
			conditionalTarget = conditional->operands()[1].basicBlock();
		if (conditionalTarget != fallthrough)
			continue;

		if (!invertConditional(*conditional, fallback))
			continue;
		instructions.erase(unconditional);
		changed = true;
	}
	return changed;
}

bool AArch64BranchRelaxation::run(MachineFunction &function) {

	bool changed = false;
	unsigned splitNumber = 0;
	for (;;) {
		std::unordered_map<MachineBasicBlock *, std::int64_t> blockOffsets;
		std::unordered_map<const MachineInstr *, std::int64_t>
		    instructionOffsets;
		std::int64_t offset = 0;
		for (const auto &owned : function.blocks()) {
			blockOffsets[owned.get()] = offset;
			for (const MachineInstr &instruction : owned->instructions()) {
				instructionOffsets[&instruction] = offset;
				offset += emittedInstructionSize(instruction);
			}
		}

		MachineBasicBlock *source = nullptr;
		MachineBasicBlock::InstrList::iterator branch;
		MachineBasicBlock *target = nullptr;
		bool found = false;
		for (const auto &owned : function.blocks()) {
			auto &instructions = owned->instructions();
			for (auto it = instructions.begin(); it != instructions.end();
			     ++it) {
				const Opcode opcode = it->opcode();
				if (opcode != Opcode::Bcc && opcode != Opcode::CBZ &&
				    opcode != Opcode::CBNZ && opcode != Opcode::TBZ &&
				    opcode != Opcode::TBNZ)
					continue;
				const unsigned operand = branchTargetOperand(opcode);
				if (it->operands().size() <= operand ||
				    it->operands()[operand].kind() !=
				        MachineOperand::Kind::BasicBlock)
					std::abort();
				MachineBasicBlock *candidate =
				    it->operands()[operand].basicBlock();
				const std::int64_t displacement =
				    blockOffsets.at(candidate) - instructionOffsets.at(&*it);
				if (branchDisplacementFits(opcode, displacement))
					continue;
				source = owned.get();
				branch = it;
				target = candidate;
				found = true;
				break;
			}
			if (found)
				break;
		}
		if (!found)
			break;

		std::vector<MachineBasicBlock *> originalSuccessors =
		    source->successors();
		if (originalSuccessors.size() != 2 ||
		    std::find(originalSuccessors.begin(), originalSuccessors.end(),
		              target) == originalSuccessors.end())
			std::abort();

		MachineBasicBlock &continuation = function.createBlock(
		    "branch.relax." + std::to_string(splitNumber++));
		continuation.frequency = source->frequency;
		continuation.loopDepth = source->loopDepth;

		auto &blocks = function.blocks();
		auto sourcePosition = blocks.begin();
		while (sourcePosition != blocks.end() &&
		       sourcePosition->get() != source)
			++sourcePosition;
		std::unique_ptr<MachineBasicBlock> continuationOwner =
		    std::move(blocks.back());
		blocks.pop_back();
		sourcePosition = blocks.begin();
		while (sourcePosition != blocks.end() &&
		       sourcePosition->get() != source)
			++sourcePosition;
		blocks.insert(std::next(sourcePosition), std::move(continuationOwner));

		auto afterBranch = std::next(branch);
		continuation.instructions().splice(continuation.instructions().end(),
		                                   source->instructions(), afterBranch,
		                                   source->instructions().end());

		for (MachineBasicBlock *successor : originalSuccessors)
			source->removeSuccessor(successor);
		source->addSuccessor(target);
		source->addSuccessor(&continuation);
		for (MachineBasicBlock *successor : originalSuccessors)
			if (successor != target)
				continuation.addSuccessor(successor);

		switch (branch->opcode()) {
		case Opcode::Bcc:
			branch->operands()[0] = MachineOperand::condition(
			    InstrInfo::inverseCondition(branch->operands()[0].condition()));
			branch->operands()[1] = MachineOperand::block(&continuation);
			break;
		case Opcode::CBZ:
		case Opcode::CBNZ:
			branch->setOpcode(branch->opcode() == Opcode::CBZ ? Opcode::CBNZ
			                                                  : Opcode::CBZ);
			branch->operands()[1] = MachineOperand::block(&continuation);
			break;
		case Opcode::TBZ:
		case Opcode::TBNZ:
			branch->setOpcode(branch->opcode() == Opcode::TBZ ? Opcode::TBNZ
			                                                  : Opcode::TBZ);
			branch->operands()[2] = MachineOperand::block(&continuation);
			break;
		default:
			std::abort();
		}

		MachineInstr longBranch(Opcode::B);
		longBranch.addOperand(MachineOperand::block(target));
		source->instructions().insert(std::next(branch), std::move(longBranch));
		changed = true;
	}

	function.setProperty(MachineProperty::BranchesRelaxed);
	return changed;
}

} // namespace backend::aarch64
