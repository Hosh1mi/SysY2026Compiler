#include "backend/frame_lowering.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

unsigned alignTo(unsigned value, unsigned alignment) {
	return (value + alignment - 1) / alignment * alignment;
}

bool isVectorRegister(PhysReg reg) {
	return reg >= PhysReg::V0 && reg <= PhysReg::V31;
}

bool isPairOffsetEncodable(std::int64_t offset, unsigned scale) {
	return offset % scale == 0 && offset / scale >= -64 && offset / scale <= 63;
}

bool isScaledOffsetEncodable(std::int64_t offset, unsigned scale) {
	return offset >= 0 && offset % scale == 0 && offset / scale <= 4095;
}

MachineOperand phys(PhysReg reg, RegClass regClass, bool isDef = false) {
	return MachineOperand::physReg(reg, regClass, isDef);
}

void addMemoryInfo(MachineInstr &instruction, MachineMemOperand::Access access,
                   unsigned size, unsigned alignment,
                   std::optional<int> frameIndex, std::int64_t offset) {
	instruction.addMemoryOperand(MachineMemOperand{
	    access, size, alignment, nullptr, frameIndex, offset, false});
}

} // namespace

void AArch64FrameLowering::determineCalleeSaves(
    MachineFunction &function) const {
	std::unordered_set<PhysReg> used;
	for (const auto &block : function.blocks())
		for (const MachineInstr &instruction : block->instructions())
			for (const MachineOperand &operand : instruction.operands())
				if (operand.isPhysicalRegister() &&
				    RegisterInfo::isCalleeSaved(operand.physicalRegister()))
					used.insert(operand.physicalRegister());

	auto &saved = function.frameInfo().savedRegisters;
	saved.assign(used.begin(), used.end());
	std::sort(saved.begin(), saved.end(), [](PhysReg lhs, PhysReg rhs) {
		return static_cast<unsigned>(lhs) < static_cast<unsigned>(rhs);
	});
}

void AArch64FrameLowering::layoutFrame(MachineFunction &function) const {
	MachineFrameInfo &frame = function.frameInfo();
	// Reserve a stable outgoing-argument area at the bottom of the frame.
	// SP therefore never moves around a call, so spill and local frame
	// references remain valid while call arguments are prepared.
	unsigned cursor = frame.maxCallFrameSize;
	for (PhysReg reg : frame.savedRegisters) {
		// AAPCS64 preserves only the low 64 bits of v8-v15.
		unsigned size = 8;
		cursor = alignTo(cursor, size);
		frame.savedRegisterOffsets[reg] = static_cast<int>(cursor);
		cursor += size;
	}
	for (StackObject &object : frame.objects()) {
		if (object.fixed) {
			// x29 is established after the 16-byte FP/LR push, so the
			// caller's incoming stack argument area starts at x29 + 16.
			object.offset += 16;
		}
	}
	auto layoutObjects = [&](bool spills) {
		for (StackObject &object : frame.objects()) {
			if (object.fixed || object.spill != spills)
				continue;
			cursor = alignTo(cursor, object.alignment);
			object.offset = static_cast<int>(cursor);
			cursor += object.size;
		}
	};
	// Keep allocator spill slots in the low, directly encodable SP window.
	// Large local arrays are addressed through LEA_FRAME values and can sit
	// above them without adding work to each dynamic spill/reload.
	layoutObjects(true);
	layoutObjects(false);
	frame.stackSize = alignTo(cursor, 16);
	bool hasFixedObject = false;
	for (const StackObject &object : frame.objects())
		if (object.fixed) {
			hasFixedObject = true;
			break;
		}
	// Incoming stack arguments are addressed from x29 after the FP/LR push.
	// Even when the body needs no locals, spills, or callee-saves, a frame
	// pointer is still required whenever those fixed objects exist.
	frame.usesFramePointer = frame.stackSize != 0 || frame.hasCalls ||
	                         !frame.savedRegisters.empty() || hasFixedObject;
}

void AArch64FrameLowering::eliminateFrameIndices(
    MachineFunction &function) const {
	MachineFrameInfo &frame = function.frameInfo();
	for (auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end();) {
			if (it->opcode() == Opcode::ADJCALLSTACKDOWN ||
			    it->opcode() == Opcode::ADJCALLSTACKUP) {
				if (it->operands().size() != 1 ||
				    it->operands()[0].kind() != MachineOperand::Kind::Immediate)
					throw std::logic_error("malformed call-stack adjustment");
				it = instructions.erase(it);
				continue;
			}
			if (it->opcode() == Opcode::LEA_FRAME) {
				if (it->operands().size() != 2 ||
				    !it->operands()[0].isPhysicalRegister() ||
				    it->operands()[1].kind() !=
				        MachineOperand::Kind::FrameIndex)
					throw std::logic_error("malformed LEA_FRAME");
				const StackObject &object =
				    frame.getObject(it->operands()[1].frameIndex());
				PhysReg base = object.fixed ? PhysReg::X29 : PhysReg::SP;
				std::int64_t offset = object.offset;
				MachineOperand destination = it->operands()[0];
				if (offset < 0)
					throw std::logic_error(
					    "negative frame address is unsupported");
				unsigned amount =
				    static_cast<unsigned>(std::min<std::int64_t>(offset, 4095));
				it->operands().clear();
				if (amount == 0) {
					it->setOpcode(Opcode::MOVXrr);
					it->addOperand(destination)
					    .addOperand(phys(base, RegClass::GPR64));
				} else {
					it->setOpcode(Opcode::ADDXri);
					it->addOperand(destination)
					    .addOperand(phys(base, RegClass::GPR64))
					    .addOperand(MachineOperand::immediate(amount));
				}
				offset -= amount;
				auto insertion = std::next(it);
				while (offset) {
					amount = static_cast<unsigned>(
					    std::min<std::int64_t>(offset, 4095));
					MachineInstr add(Opcode::ADDXri);
					add.addOperand(destination)
					    .addOperand(MachineOperand::physReg(
					        destination.physicalRegister(), RegClass::GPR64))
					    .addOperand(MachineOperand::immediate(amount));
					instructions.insert(insertion, std::move(add));
					offset -= amount;
				}
				it = insertion;
				continue;
			}

			if (it->opcode() == Opcode::SPILL_LOAD ||
			    it->opcode() == Opcode::SPILL_STORE) {
				bool load = it->opcode() == Opcode::SPILL_LOAD;
				if (it->operands().size() != 2 ||
				    !it->operands()[0].isPhysicalRegister() ||
				    it->operands()[1].kind() !=
				        MachineOperand::Kind::FrameIndex)
					throw std::logic_error("malformed spill pseudo");
				int frameIndex = it->operands()[1].frameIndex();
				const StackObject &object = frame.getObject(frameIndex);
				RegClass regClass = it->operands()[0].regClass();
				unsigned width = regClass == RegClass::NEON128 ? 16U
				                 : regClass == RegClass::GPR64 ? 8U
				                                               : 4U;
				if (load)
					it->setOpcode(
					    regClass == RegClass::FPR32     ? Opcode::LDRSui
					    : regClass == RegClass::NEON128 ? Opcode::LDRQui
					    : regClass == RegClass::GPR64   ? Opcode::LDRXui
					                                    : Opcode::LDRWui);
				else
					it->setOpcode(
					    regClass == RegClass::FPR32     ? Opcode::STRSui
					    : regClass == RegClass::NEON128 ? Opcode::STRQui
					    : regClass == RegClass::GPR64   ? Opcode::STRXui
					                                    : Opcode::STRWui);
				MachineOperand value = it->operands()[0];
				std::int64_t absoluteOffset =
				    object.fixed ? static_cast<std::int64_t>(frame.stackSize) +
				                       object.offset
				                 : object.offset;
				std::int64_t memoryOffset = object.offset;
				PhysReg base = object.fixed ? PhysReg::X29 : PhysReg::SP;
				if (!isScaledOffsetEncodable(memoryOffset, width)) {
					// Frame offsets are known only after spilling.  When an
					// offset is beyond the unsigned scaled memory encoding,
					// adjust SP explicitly around the access.  The changes
					// are represented in final MIR and remain visible to
					// scheduling and verification; no hidden scratch
					// register is introduced.
					std::uint64_t adjustment =
					    static_cast<std::uint64_t>(absoluteOffset) / 16 * 16;
					memoryOffset =
					    absoluteOffset - static_cast<std::int64_t>(adjustment);
					base = PhysReg::SP;
					std::uint64_t remaining = adjustment;
					while (remaining) {
						unsigned amount = static_cast<unsigned>(
						    std::min<std::uint64_t>(remaining, 4080));
						MachineInstr adjust(Opcode::ADDSPri);
						adjust
						    .addOperand(
						        phys(PhysReg::SP, RegClass::GPR64, true))
						    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
						    .addOperand(MachineOperand::immediate(amount));
						instructions.insert(it, std::move(adjust));
						remaining -= amount;
					}
					auto restorePosition = std::next(it);
					remaining = adjustment;
					while (remaining) {
						unsigned amount = static_cast<unsigned>(
						    std::min<std::uint64_t>(remaining, 4080));
						MachineInstr restore(Opcode::SUBSPri);
						restore
						    .addOperand(
						        phys(PhysReg::SP, RegClass::GPR64, true))
						    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
						    .addOperand(MachineOperand::immediate(amount));
						instructions.insert(restorePosition,
						                    std::move(restore));
						remaining -= amount;
					}
				}
				it->operands().clear();
				it->addOperand(std::move(value))
				    .addOperand(phys(base, RegClass::GPR64))
				    .addOperand(MachineOperand::immediate(memoryOffset));
				++it;
				continue;
			}
			++it;
		}
	}
}

void AArch64FrameLowering::insertPrologueEpilogues(
    MachineFunction &function) const {
	MachineFrameInfo &frame = function.frameInfo();
	if (!frame.usesFramePointer)
		return;

	MachineBasicBlock *entry = function.entryBlock();
	if (!entry)
		throw std::logic_error("cannot insert prologue without entry");
	auto insertion = entry->instructions().begin();

	MachineInstr push(Opcode::STPXpre);
	push.addOperand(phys(PhysReg::X29, RegClass::GPR64))
	    .addOperand(phys(PhysReg::X30, RegClass::GPR64))
	    .addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
	    .addOperand(MachineOperand::immediate(-16));
	addMemoryInfo(push, MachineMemOperand::Access::Store, 16, 16, std::nullopt,
	              -16);
	entry->instructions().insert(insertion, std::move(push));

	MachineInstr setFrame(Opcode::MOVXrr);
	setFrame.addOperand(phys(PhysReg::X29, RegClass::GPR64, true))
	    .addOperand(phys(PhysReg::SP, RegClass::GPR64));
	entry->instructions().insert(insertion, std::move(setFrame));

	unsigned remaining = frame.stackSize;
	while (remaining) {
		unsigned amount = std::min(remaining, 4095U);
		MachineInstr adjust(Opcode::SUBSPri);
		adjust.addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
		    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
		    .addOperand(MachineOperand::immediate(amount));
		entry->instructions().insert(insertion, std::move(adjust));
		remaining -= amount;
	}

	for (std::size_t i = 0; i < frame.savedRegisters.size();) {
		PhysReg reg = frame.savedRegisters[i];
		bool vector = isVectorRegister(reg);
		unsigned size = 8;
		if (i + 1 < frame.savedRegisters.size()) {
			PhysReg next = frame.savedRegisters[i + 1];
			bool nextVector = isVectorRegister(next);
			int offset = frame.savedRegisterOffsets.at(reg);
			if (vector == nextVector &&
			    frame.savedRegisterOffsets.at(next) ==
			        offset + static_cast<int>(size) &&
			    isPairOffsetEncodable(offset, size)) {
				MachineInstr save(vector ? Opcode::STPDi : Opcode::STPXi);
				save.addOperand(
				        phys(reg, vector ? RegClass::NEON128 : RegClass::GPR64))
				    .addOperand(phys(next, vector ? RegClass::NEON128
				                                  : RegClass::GPR64))
				    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
				    .addOperand(MachineOperand::immediate(offset));
				addMemoryInfo(save, MachineMemOperand::Access::Store, size * 2,
				              size, std::nullopt, offset);
				entry->instructions().insert(insertion, std::move(save));
				i += 2;
				continue;
			}
		}
		MachineInstr save(vector ? Opcode::STRDui : Opcode::STRXui);
		save.addOperand(phys(reg, vector ? RegClass::NEON128 : RegClass::GPR64))
		    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
		    .addOperand(
		        MachineOperand::immediate(frame.savedRegisterOffsets.at(reg)));
		addMemoryInfo(save, MachineMemOperand::Access::Store, 8, 8,
		              std::nullopt, frame.savedRegisterOffsets.at(reg));
		entry->instructions().insert(insertion, std::move(save));
		++i;
	}

	for (auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end(); ++it) {
			if (it->opcode() != Opcode::RET && it->opcode() != Opcode::TAILCALL)
				continue;
			for (std::size_t i = frame.savedRegisters.size(); i > 0;) {
				if (i >= 2) {
					PhysReg first = frame.savedRegisters[i - 2];
					PhysReg second = frame.savedRegisters[i - 1];
					bool vector = isVectorRegister(first);
					unsigned size = 8;
					int offset = frame.savedRegisterOffsets.at(first);
					if (vector == isVectorRegister(second) &&
					    frame.savedRegisterOffsets.at(second) ==
					        offset + static_cast<int>(size) &&
					    isPairOffsetEncodable(offset, size)) {
						MachineInstr restore(vector ? Opcode::LDPDi
						                            : Opcode::LDPXi);
						restore
						    .addOperand(phys(first,
						                     vector ? RegClass::NEON128
						                            : RegClass::GPR64,
						                     true))
						    .addOperand(phys(second,
						                     vector ? RegClass::NEON128
						                            : RegClass::GPR64,
						                     true))
						    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
						    .addOperand(MachineOperand::immediate(offset));
						addMemoryInfo(restore, MachineMemOperand::Access::Load,
						              size * 2, size, std::nullopt, offset);
						instructions.insert(it, std::move(restore));
						i -= 2;
						continue;
					}
				}
				PhysReg saved = frame.savedRegisters[i - 1];
				bool vector = isVectorRegister(saved);
				MachineInstr restore(vector ? Opcode::LDRDui : Opcode::LDRXui);
				restore
				    .addOperand(phys(
				        saved, vector ? RegClass::NEON128 : RegClass::GPR64,
				        true))
				    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
				    .addOperand(MachineOperand::immediate(
				        frame.savedRegisterOffsets.at(saved)));
				addMemoryInfo(restore, MachineMemOperand::Access::Load, 8, 8,
				              std::nullopt,
				              frame.savedRegisterOffsets.at(saved));
				instructions.insert(it, std::move(restore));
				--i;
			}
			unsigned restore = frame.stackSize;
			while (restore) {
				unsigned amount = std::min(restore, 4095U);
				MachineInstr adjust(Opcode::ADDSPri);
				adjust.addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
				    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
				    .addOperand(MachineOperand::immediate(amount));
				instructions.insert(it, std::move(adjust));
				restore -= amount;
			}
			MachineInstr pop(Opcode::LDPXpost);
			pop.addOperand(phys(PhysReg::X29, RegClass::GPR64, true))
			    .addOperand(phys(PhysReg::X30, RegClass::GPR64, true))
			    .addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
			    .addOperand(MachineOperand::immediate(16));
			addMemoryInfo(pop, MachineMemOperand::Access::Load, 16, 16,
			              std::nullopt, 0);
			instructions.insert(it, std::move(pop));
		}
	}
}

void AArch64FrameLowering::run(MachineFunction &function) const {
	if (!function.hasProperty(MachineProperty::NoVRegs))
		throw std::logic_error("frame lowering requires NoVRegs");
	determineCalleeSaves(function);
	layoutFrame(function);
	eliminateFrameIndices(function);
	insertPrologueEpilogues(function);
	function.setProperty(MachineProperty::FrameFinalized);
}

} // namespace backend::aarch64
