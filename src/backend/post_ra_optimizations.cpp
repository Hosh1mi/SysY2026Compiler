// Post-register-allocation optimizations operate on physical registers and
// lower remaining machine pseudos before final scheduling.
#include "../include/backend/post_ra_optimizations.hpp"
#include "../include/backend/machine_analysis.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

using PhysSet = std::unordered_set<PhysReg>;
using BlockPhysSets = std::unordered_map<MachineBasicBlock *, PhysSet>;

void addPhysicalUse(BlockPhysSets &physicalUses,
                    const BlockPhysSets &physicalDefs,
                    MachineBasicBlock *block, PhysReg reg) {
	BlockPhysSets::const_iterator found = physicalDefs.find(block);
	if (found == physicalDefs.end() || !found->second.count(reg))
		physicalUses[block].insert(reg);
}

std::uint64_t moveWidePiece(std::uint64_t value, unsigned index) {
	return (value >> (index * 16)) & 0xffffU;
}

void insertMoveWide(MachineBasicBlock::InstrList &instructions,
                    MachineBasicBlock::InstrList::iterator position,
                    Opcode opcode, PhysReg reg, RegClass regClass,
                    unsigned slice, std::uint64_t immediate, bool tied) {
	MachineInstr move(opcode);
	move.addOperand(MachineOperand::physReg(reg, regClass, true));
	if (tied) {
		MachineOperand use = MachineOperand::physReg(reg, regClass);
		use.tiedTo = 0;
		use.isKill = true;
		move.addOperand(std::move(use));
	}
	move.addOperand(MachineOperand::immediate(
	                   static_cast<std::int64_t>(immediate)))
	    .addOperand(MachineOperand::immediate(
	                   static_cast<std::int64_t>(slice * 16)));
	instructions.insert(position, std::move(move));
}

bool readsPhysicalRegister(const MachineInstr &instruction, PhysReg reg) {
	if (instruction.readsRegister(reg))
		return true;
	if (instruction.isCall() && RegisterInfo::isArgumentRegister(reg))
		return true;
	return instruction.opcode() == Opcode::RET &&
	       (RegisterInfo::isReturnRegister(reg) || reg == PhysReg::X30);
}

BlockPhysSets computePhysicalLiveOut(MachineFunction &function) {
	BlockPhysSets physicalUses;
	BlockPhysSets physicalDefs;
	BlockPhysSets physicalLiveIn;
	BlockPhysSets physicalLiveOut;
	for (auto &owned : function.blocks()) {
		MachineBasicBlock *block = owned.get();
		for (const MachineInstr &instruction : block->instructions()) {
			for (const MachineOperand &operand : instruction.operands())
				if (operand.isPhysicalRegister() && !operand.isDef)
					addPhysicalUse(physicalUses, physicalDefs, block,
					               operand.physicalRegister());
			if (instruction.isCall()) {
				for (unsigned index = 0; index < 8; ++index) {
					addPhysicalUse(physicalUses, physicalDefs, block,
					               RegisterInfo::integerArgumentRegister(index));
					addPhysicalUse(physicalUses, physicalDefs, block,
					               RegisterInfo::vectorArgumentRegister(index));
				}
			} else if (instruction.opcode() == Opcode::RET) {
				addPhysicalUse(physicalUses, physicalDefs, block, PhysReg::X0);
				addPhysicalUse(physicalUses, physicalDefs, block, PhysReg::V0);
				addPhysicalUse(physicalUses, physicalDefs, block, PhysReg::X30);
			}

			// Uses precede definitions for read/modify/write instructions.
			for (const MachineOperand &operand : instruction.operands())
				if (operand.isPhysicalRegister() && operand.isDef)
					physicalDefs[block].insert(operand.physicalRegister());
			if (instruction.isCall()) {
				for (unsigned raw = static_cast<unsigned>(PhysReg::X0);
				     raw <= static_cast<unsigned>(PhysReg::V31); ++raw) {
					PhysReg reg = static_cast<PhysReg>(raw);
					if (RegisterInfo::isCallerSaved(reg))
						physicalDefs[block].insert(reg);
				}
			}
		}
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (auto block = function.blocks().rbegin();
		     block != function.blocks().rend(); ++block) {
			MachineBasicBlock *current = block->get();
			PhysSet nextOut;
			for (MachineBasicBlock *successor : current->successors())
				nextOut.insert(physicalLiveIn[successor].begin(),
				               physicalLiveIn[successor].end());
			PhysSet nextIn = physicalUses[current];
			for (PhysReg reg : nextOut)
				if (!physicalDefs[current].count(reg))
					nextIn.insert(reg);
			if (nextOut != physicalLiveOut[current] ||
			    nextIn != physicalLiveIn[current]) {
				physicalLiveOut[current] = std::move(nextOut);
				physicalLiveIn[current] = std::move(nextIn);
				changed = true;
			}
		}
	}
	return physicalLiveOut;
}

bool isRegisterLiveAfter(MachineBasicBlock::InstrList::const_iterator scan,
                         MachineBasicBlock::InstrList::const_iterator end,
                         PhysReg reg, const PhysSet &liveOut) {
	for (; scan != end; ++scan) {
		if (readsPhysicalRegister(*scan, reg))
			return true;
		if (scan->definesRegister(reg))
			return false;
	}
	return liveOut.count(reg);
}

} // namespace

bool PostRACopyPropagation::run(MachineFunction &function) {

	const BlockPhysSets physicalLiveOut = computePhysicalLiveOut(function);
	bool changed = false;
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto producer = instructions.begin();
		     producer != instructions.end(); ++producer) {
			switch (producer->opcode()) {
			case Opcode::ADDWri:
			case Opcode::SUBWri:
			case Opcode::LSLWri:
			case Opcode::LSRWri:
			case Opcode::ASRWri:
			case Opcode::ADDXri:
			case Opcode::SUBXri:
			case Opcode::LSLXri:
			case Opcode::ASRXri:
				break;
			default:
				continue;
			}
			if (producer->operands().size() < 2 ||
			    !producer->operands()[0].isPhysicalRegister() ||
			    !producer->operands()[0].isDef ||
			    !producer->operands()[1].isPhysicalRegister())
				continue;
			PhysReg temporary = producer->operands()[0].physicalRegister();
			PhysReg input = producer->operands()[1].physicalRegister();
			if (RegisterInfo::aliases(temporary, input))
				continue;

			auto copy = std::next(producer);
			for (unsigned distance = 0;
			     copy != instructions.end() && distance < 6;
			     ++copy, ++distance) {
				if (copy->isCall() || copy->isTerminator())
					break;
				if (copy->opcode() == Opcode::COPY &&
				    copy->operands().size() == 2 &&
				    copy->operands()[0].isPhysicalRegister() &&
				    copy->operands()[0].isDef &&
				    copy->operands()[1].isPhysicalRegister() &&
				    RegisterInfo::aliases(
				        copy->operands()[0].physicalRegister(), input) &&
				    RegisterInfo::aliases(
				        copy->operands()[1].physicalRegister(), temporary) &&
				    !isRegisterLiveAfter(std::next(copy), instructions.end(),
				                         temporary,
				                         physicalLiveOut.at(block.get()))) {
					producer->operands()[0] = MachineOperand::physReg(
					    input, producer->operands()[0].regClass(), true);
					instructions.erase(copy);
					changed = true;
					break;
				}

				bool conflicts = false;
				for (const MachineOperand &operand : copy->operands())
					if (operand.isPhysicalRegister() &&
					    (RegisterInfo::aliases(operand.physicalRegister(),
					                           temporary) ||
					     RegisterInfo::aliases(operand.physicalRegister(),
					                           input))) {
						conflicts = true;
						break;
					}
				if (conflicts)
					break;
			}
		}
	}

	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto instruction = instructions.begin();
		     instruction != instructions.end();) {
			if (instruction->opcode() == Opcode::COPY &&
			    instruction->operands().size() == 2 &&
			    instruction->operands()[0].isPhysicalRegister() &&
			    instruction->operands()[1].isPhysicalRegister() &&
			    instruction->operands()[0].isSameRegisterAs(
			        instruction->operands()[1])) {
				instruction = instructions.erase(instruction);
				changed = true;
				continue;
			}
			++instruction;
		}
	}

	// Forward a physical copy through its local uses until the copied value
	// is provably killed.  This handles call results such as
	// `COPY s16, s0` without treating calls as transparent: argument
	// registers are implicit call uses, caller-saved temporaries are call
	// clobbers, and branch edges remain conservatively live-out.
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto copy = instructions.begin(); copy != instructions.end();) {
			if (copy->opcode() != Opcode::COPY ||
			    copy->operands().size() != 2 ||
			    !copy->operands()[0].isPhysicalRegister() ||
			    !copy->operands()[0].isDef ||
			    !copy->operands()[1].isPhysicalRegister() ||
			    copy->operands()[0].regClass() !=
			        copy->operands()[1].regClass()) {
				++copy;
				continue;
			}
			PhysReg destination = copy->operands()[0].physicalRegister();
			PhysReg source = copy->operands()[1].physicalRegister();
			if (RegisterInfo::aliases(destination, source)) {
				++copy;
				continue;
			}

			bool sourceAvailable = true;
			bool killed = false;
			bool blocked = false;
			std::vector<MachineOperand *> rewrites;
			for (auto scan = std::next(copy); scan != instructions.end();
			     ++scan) {
				if (scan->isCall()) {
					if (RegisterInfo::isArgumentRegister(destination)) {
						blocked = true;
					} else if (RegisterInfo::isCallerSaved(destination)) {
						killed = true;
					}
					break;
				}
				if (scan->isTerminator()) {
					bool terminatorUsesDestination = false;
					for (const MachineOperand &operand : scan->operands())
						if (operand.isPhysicalRegister() && !operand.isDef &&
						    RegisterInfo::aliases(operand.physicalRegister(),
						                          destination)) {
							terminatorUsesDestination = true;
							break;
						}
					// A value consumed by the terminator is not necessarily
					// live-out: conditional branches use it on the current
					// block's outgoing edge.  Keep the copy in that case.
					if (terminatorUsesDestination) {
						blocked = true;
					} else if (!physicalLiveOut.at(block.get()).count(
					               destination) &&
					           (scan->opcode() != Opcode::RET ||
					            (!RegisterInfo::isReturnRegister(destination) &&
					             destination != PhysReg::X30)))
						killed = true;
					else
						blocked = true;
					break;
				}
				bool definesDestination = false;
				bool definesSource = false;
				for (MachineOperand &operand : scan->operands()) {
					if (!operand.isPhysicalRegister())
						continue;
					PhysReg reg = operand.physicalRegister();
					if (operand.isDef) {
						definesDestination |=
						    RegisterInfo::aliases(reg, destination);
						definesSource |= RegisterInfo::aliases(reg, source);
						continue;
					}
					if (!RegisterInfo::aliases(reg, destination))
						continue;
					if (!sourceAvailable || operand.tiedTo >= 0 ||
					    operand.regClass() != copy->operands()[1].regClass()) {
						blocked = true;
						break;
					}
					rewrites.push_back(&operand);
				}
				if (blocked)
					break;
				if (definesDestination) {
					killed = true;
					break;
				}
				if (definesSource)
					sourceAvailable = false;
			}
			if (blocked || !killed) {
				++copy;
				continue;
			}
			for (MachineOperand *operand : rewrites) {
				bool implicit = operand->isImplicit;
				*operand = MachineOperand::physReg(
				    source, copy->operands()[1].regClass(), false, implicit);
			}
			copy = instructions.erase(copy);
			changed = true;
		}
	}

	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto copy = instructions.begin(); copy != instructions.end();) {
			if (copy->opcode() != Opcode::COPY ||
			    copy->operands().size() != 2 ||
			    !copy->operands()[0].isPhysicalRegister() ||
			    !copy->operands()[1].isPhysicalRegister()) {
				++copy;
				continue;
			}
			PhysReg destination = copy->operands()[0].physicalRegister();
			PhysReg source = copy->operands()[1].physicalRegister();
			RegClass destinationClass = copy->operands()[0].regClass();
			RegClass sourceClass = copy->operands()[1].regClass();
			if (destinationClass != sourceClass) {
				++copy;
				continue;
			}

			bool reachedRedefinition = false;
			bool blocked = false;
			std::vector<MachineOperand *> rewrites;
			for (auto scan = std::next(copy); scan != instructions.end();
			     ++scan) {
				if (scan->isCall() || scan->isTerminator() ||
				    scan->hasSideEffects()) {
					blocked = true;
					break;
				}
				bool definesDestination = false;
				bool definesSource = false;
				for (MachineOperand &operand : scan->operands()) {
					if (!operand.isPhysicalRegister())
						continue;
					PhysReg reg = operand.physicalRegister();
					if (operand.isDef) {
						definesDestination |=
						    RegisterInfo::aliases(reg, destination);
						definesSource |= RegisterInfo::aliases(reg, source);
					} else if (RegisterInfo::aliases(reg, destination)) {
						if (operand.tiedTo >= 0) {
							blocked = true;
							break;
						}
						if (operand.regClass() != sourceClass) {
							blocked = true;
							break;
						}
						rewrites.push_back(&operand);
					}
				}
				if (blocked)
					break;
				if (definesSource && !definesDestination) {
					blocked = true;
					break;
				}
				if (definesDestination) {
					reachedRedefinition = true;
					break;
				}
			}
			if (blocked || !reachedRedefinition) {
				++copy;
				continue;
			}
			for (MachineOperand *operand : rewrites) {
				bool isDef = operand->isDef;
				bool implicit = operand->isImplicit;
				*operand = MachineOperand::physReg(source, sourceClass, isDef,
				                                   implicit);
			}
			copy = instructions.erase(copy);
			changed = true;
		}
	}
	return changed;
}

namespace {

unsigned fpRegisterColor(PhysReg reg) {
    return (static_cast<unsigned>(reg) -
            static_cast<unsigned>(PhysReg::V0)) & 1U;
}

bool samePreservationClass(PhysReg lhs, PhysReg rhs) {
    return RegisterInfo::isCallerSaved(lhs) ==
               RegisterInfo::isCallerSaved(rhs) &&
           RegisterInfo::isCalleeSaved(lhs) ==
               RegisterInfo::isCalleeSaved(rhs);
}

bool instructionTouchesRegister(const MachineInstr &instruction,
                                PhysReg reg) {
    if (instruction.readsRegister(reg) ||
        instruction.definesRegister(reg))
        return true;
    if (instruction.isCall() &&
        RegisterInfo::isArgumentRegister(reg))
        return true;
    return instruction.opcode() == Opcode::RET &&
           RegisterInfo::isReturnRegister(reg);
}

struct FPValueChain {
    bool renamable = false;
    std::size_t end = 0;
    std::vector<MachineOperand *> uses;
};

FPValueChain findFPValueChain(
    MachineBasicBlock &block, const std::vector<MachineInstr *> &instructions,
    std::size_t start, PhysReg reg,
    const MachinePhysicalRegisterLiveness &liveness) {
    FPValueChain chain;
    bool redefined = false;
    for (std::size_t index = start + 1; index < instructions.size(); ++index) {
        MachineInstr &instruction = *instructions[index];
        if ((instruction.isCall() &&
             RegisterInfo::isArgumentRegister(reg)) ||
            (instruction.opcode() == Opcode::RET &&
             RegisterInfo::isReturnRegister(reg)))
            return chain;

        for (MachineOperand &operand : instruction.operands()) {
            if (!operand.isPhysicalRegister() || operand.isDef ||
                !RegisterInfo::aliases(operand.physicalRegister(), reg))
                continue;
            if (operand.isImplicit || !operand.isRenamable ||
                operand.tiedTo >= 0 ||
                operand.regClass() != RegClass::FPR32)
                return chain;
            chain.uses.push_back(&operand);
            chain.end = index;
        }
        if (instruction.definesRegister(reg)) {
            redefined = true;
            break;
        }
    }

    if (!redefined && liveness.liveOut(&block).count(reg))
        return FPValueChain{};
    chain.renamable = !chain.uses.empty();
    return chain;
}

} // namespace

bool A53FPRegisterBalancing::run(MachineFunction &function) {
    MachinePhysicalRegisterLiveness liveness(function);
    bool changed = false;
    for (auto &owned : function.blocks()) {
        MachineBasicBlock &block = *owned;
        std::vector<MachineInstr *> instructions;
        instructions.reserve(block.instructions().size());
        for (MachineInstr &instruction : block.instructions())
            instructions.push_back(&instruction);

        struct OccupiedRange {
            PhysReg reg;
            std::size_t begin;
            std::size_t end;
        };
        std::vector<OccupiedRange> recoloredRanges;
        int balance = 0;
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            MachineInstr &multiply = *instructions[index];
            if (multiply.opcode() != Opcode::FMULS ||
                multiply.operands().empty() ||
                !multiply.operands()[0].isPhysicalRegister() ||
                !multiply.operands()[0].isDef ||
                multiply.operands()[0].regClass() != RegClass::FPR32)
                continue;

            MachineOperand &definition = multiply.operands()[0];
            PhysReg original = definition.physicalRegister();
            unsigned originalColor = fpRegisterColor(original);
            unsigned desiredColor = balance > 0 ? 1U
                                  : balance < 0 ? 0U
                                                : originalColor;
            PhysReg selected = original;

            if (desiredColor != originalColor && !definition.isImplicit &&
                definition.isRenamable && definition.tiedTo < 0) {
                FPValueChain chain = findFPValueChain(
                    block, instructions, index, original, liveness);
                if (chain.renamable) {
                    for (PhysReg candidate :
                         RegisterInfo::allocationOrder(RegClass::FPR32)) {
                        if (candidate == original ||
                            !RegisterInfo::isVector(candidate) ||
                            RegisterInfo::isReserved(candidate) ||
                            fpRegisterColor(candidate) != desiredColor ||
                            !samePreservationClass(original, candidate) ||
                            liveness.isLiveBefore(&multiply, candidate))
                            continue;

                        bool unavailable = false;
                        for (std::size_t scan = index; scan <= chain.end;
                             ++scan)
                            if (instructionTouchesRegister(
                                    *instructions[scan], candidate)) {
                                unavailable = true;
                                break;
                            }
                        if (unavailable)
                            continue;
                        for (const OccupiedRange &range : recoloredRanges)
                            if (range.reg == candidate &&
                                index <= range.end &&
                                range.begin <= chain.end) {
                                unavailable = true;
                                break;
                            }
                        if (unavailable)
                            continue;

                        selected = candidate;
                        definition.replacePhysicalRegister(candidate);
                        for (MachineOperand *use : chain.uses)
                            use->replacePhysicalRegister(candidate);
                        recoloredRanges.push_back(
                            {candidate, index, chain.end});
                        changed = true;
                        break;
                    }
                }
            }
            balance += fpRegisterColor(selected) == 0 ? 1 : -1;
        }
    }
    return changed;
}

bool PostRARedundantCopyElimination::run(MachineFunction &function) {
	bool changed = false;
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto instruction = instructions.begin();
		     instruction != instructions.end();) {
			if (instruction->opcode() != Opcode::COPY ||
			    instruction->operands().size() != 2 ||
			    !instruction->operands()[0].isPhysicalRegister() ||
			    !instruction->operands()[1].isPhysicalRegister() ||
			    instruction->operands()[0].physicalRegister() !=
			        instruction->operands()[1].physicalRegister() ||
			    instruction->operands()[0].regClass() !=
			        instruction->operands()[1].regClass()) {
				++instruction;
				continue;
			}
			instruction = instructions.erase(instruction);
			changed = true;
		}
	}
	return changed;
}

bool PostRAInstructionExpansion::run(MachineFunction &function) {
	bool changed = false;
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto insert = instructions.begin(); insert != instructions.end();
		     ++insert) {
			const bool vectorInsert = insert->opcode() == Opcode::INSv4i32 ||
			                          insert->opcode() == Opcode::INSv4f32;
			const bool vectorAccumulate =
			    insert->opcode() == Opcode::MLAv4i32 ||
			    insert->opcode() == Opcode::MLSv4i32 ||
			    insert->opcode() == Opcode::FMLAv4f32 ||
			    insert->opcode() == Opcode::FMLSv4f32;
			const bool vectorSelect = insert->opcode() == Opcode::BSLv16i8;
			if (!vectorInsert && !vectorAccumulate && !vectorSelect)
				continue;
			if (insert->operands().size() != 4 ||
			    !insert->operands()[0].isPhysicalRegister() ||
			    !insert->operands()[0].isDef ||
			    !insert->operands()[1].isPhysicalRegister())
				std::abort();
			MachineOperand &destination = insert->operands()[0];
			MachineOperand &source = insert->operands()[1];
			if (!destination.isSameRegisterAs(source)) {
				MachineInstr copy(Opcode::COPY);
				copy.addOperand(
				        MachineOperand::physReg(destination.physicalRegister(),
				                                RegClass::NEON128, true))
				    .addOperand(MachineOperand::physReg(
				        source.physicalRegister(), RegClass::NEON128));
				instructions.insert(insert, std::move(copy));
				changed = true;
			}
		const bool needsTie =
			    !source.isSameRegisterAs(destination) ||
			    source.regClass() != RegClass::NEON128 || source.isDef ||
			    source.tiedTo != 0 || source.isKill;
			if (needsTie) {
				MachineOperand tiedUse = MachineOperand::physReg(
				    destination.physicalRegister(), RegClass::NEON128);
				tiedUse.tiedTo = 0;
				source = std::move(tiedUse);
				changed = true;
			}
		}
	}
	return changed;
}

namespace {

bool isAArch64LogicalImmediate(std::uint64_t value, unsigned width) {
	const std::uint64_t widthMask =
	    width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
	value &= widthMask;
	if (value == 0 || value == widthMask)
		return false;

	for (unsigned elementWidth = 2; elementWidth <= width; elementWidth *= 2) {
		const std::uint64_t elementMask =
		    elementWidth == 64 ? ~std::uint64_t{0}
		                       : (std::uint64_t{1} << elementWidth) - 1;
		const std::uint64_t element = value & elementMask;
		bool repeats = true;
		for (unsigned offset = elementWidth; offset < width;
		     offset += elementWidth)
			repeats &= ((value >> offset) & elementMask) == element;
		if (!repeats || element == 0 || element == elementMask)
			continue;

		for (unsigned ones = 1; ones < elementWidth; ++ones) {
			const std::uint64_t run = (std::uint64_t{1} << ones) - 1;
			for (unsigned rotation = 0; rotation < elementWidth; ++rotation) {
				const std::uint64_t rotated =
				    rotation == 0 ? run
				                  : ((run >> rotation) |
				                     (run << (elementWidth - rotation))) &
				                        elementMask;
				if (rotated == element)
					return true;
			}
		}
	}
	return false;
}

// Expand one MOVi32/MOVi64 into the shorter of the architectural MOVZ/MOVK
// and MOVN/MOVK sequences.  A tie deliberately keeps MOVZ so constants that
// gain nothing from inversion retain their established encoding.
void expandIntegerImmediate(MachineBasicBlock::InstrList &instructions,
                            MachineBasicBlock::InstrList::iterator materialize,
                            bool enableMovn, bool enableLogicalImmediate) {
	if (materialize->operands().size() != 2 ||
	    !materialize->operands()[0].isPhysicalRegister() ||
	    !materialize->operands()[0].isDef ||
	    materialize->operands()[1].kind() != MachineOperand::Kind::Immediate)
		std::abort();

	const MachineOperand &destination = materialize->operands()[0];
	const PhysReg reg = destination.physicalRegister();
	const RegClass regClass = destination.regClass();
	const bool wide = materialize->opcode() == Opcode::MOVi64;
	if ((wide && regClass != RegClass::GPR64) ||
	    (!wide && regClass != RegClass::GPR32))
		std::abort();

	const std::uint64_t value =
	    wide
	        ? static_cast<std::uint64_t>(materialize->operands()[1].immediate())
	        : static_cast<std::uint32_t>(
	              materialize->operands()[1].immediate());
	const unsigned pieces = wide ? 4U : 2U;

	unsigned nonzeroPieces = 0;
	unsigned nonOnesPieces = 0;
	for (unsigned index = 0; index < pieces; ++index) {
		nonzeroPieces += moveWidePiece(value, index) != 0;
		nonOnesPieces += moveWidePiece(value, index) != 0xffffU;
	}
	const unsigned movzCost = std::max(1U, nonzeroPieces);
	const unsigned movnCost = std::max(1U, nonOnesPieces);
	const bool useMovn = enableMovn && movnCost < movzCost;
	const unsigned moveWideCost = useMovn ? movnCost : movzCost;

	if (enableLogicalImmediate && moveWideCost > 1 &&
	    isAArch64LogicalImmediate(value, wide ? 64U : 32U)) {
		MachineInstr logicalImmediate(wide ? Opcode::ORRXri : Opcode::ORRWri);
		logicalImmediate
		    .addOperand(MachineOperand::physReg(reg, regClass, true))
		    .addOperand(MachineOperand::physReg(PhysReg::XZR, regClass))
		    .addOperand(
		        MachineOperand::immediate(static_cast<std::int64_t>(value)));
		instructions.insert(materialize, std::move(logicalImmediate));
		instructions.erase(materialize);
		return;
	}

	unsigned first = 0;
	for (unsigned index = 0; index < pieces; ++index) {
		const bool needsSeed =
		    useMovn ? moveWidePiece(value, index) != 0xffffU
		            : moveWidePiece(value, index) != 0;
		if (needsSeed) {
			first = index;
			break;
		}
	}

	if (useMovn)
		insertMoveWide(instructions, materialize, Opcode::MOVN, reg, regClass,
		               first, (~moveWidePiece(value, first)) & 0xffffU, false);
	else
		insertMoveWide(instructions, materialize, Opcode::MOVZ, reg, regClass,
		               first, moveWidePiece(value, first), false);
	for (unsigned index = 0; index < pieces; ++index) {
		if (index == first)
			continue;
		const std::uint64_t current = moveWidePiece(value, index);
		if ((useMovn && current == 0xffffU) || (!useMovn && current == 0))
			continue;
		insertMoveWide(instructions, materialize, Opcode::MOVK, reg, regClass,
		               index, current, true);
	}
	instructions.erase(materialize);
}

} // namespace

bool PostRAInstructionExpansion::expandConstantMaterializations(
    MachineFunction &function) {
	bool changed = false;
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		for (auto it = instructions.begin(); it != instructions.end();) {
			auto current = it++;
			if (current->opcode() != Opcode::MOVi32 &&
			    current->opcode() != Opcode::MOVi64)
				continue;
			expandIntegerImmediate(instructions, current, true, true);
			changed = true;
		}
	}
	return changed;
}

} // namespace backend::aarch64
