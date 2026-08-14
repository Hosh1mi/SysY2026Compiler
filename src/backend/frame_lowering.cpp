#include "backend/frame_lowering.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

constexpr unsigned kStackAlignment = 16;
constexpr unsigned kMaxAddSubImmediate = 4095;
constexpr unsigned kMaxAlignedAddSubImmediate = 4080;

enum class StackAdjustment { Allocate, Deallocate };
enum class SavedRegisterOrder { Save, Restore };

using InstrList = MachineBasicBlock::InstrList;
using InstrPosition = InstrList::iterator;

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

void insertStackAdjustment(InstrList &instructions, InstrPosition position,
                           StackAdjustment direction, std::uint64_t amount,
                           unsigned maxChunk = kMaxAddSubImmediate) {
	const Opcode opcode = direction == StackAdjustment::Allocate
	                          ? Opcode::SUBSPri
	                          : Opcode::ADDSPri;
	while (amount) {
		const unsigned chunk =
		    static_cast<unsigned>(std::min<std::uint64_t>(amount, maxChunk));
		MachineInstr adjust(opcode);
		adjust.addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
		    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
		    .addOperand(MachineOperand::immediate(chunk));
		instructions.insert(position, std::move(adjust));
		amount -= chunk;
	}
}

struct SavedRegisterGroup {
	PhysReg first;
	std::optional<PhysReg> second;
	int offset;
	bool vector;
};

std::vector<SavedRegisterGroup>
groupSavedRegisters(const MachineFrameInfo &frame, SavedRegisterOrder order) {
	std::vector<SavedRegisterGroup> groups;
	auto makeGroup = [&](std::size_t firstIndex,
	                     std::optional<std::size_t> secondIndex) {
		const PhysReg first = frame.savedRegisters[firstIndex];
		const int offset = frame.savedRegisterOffsets.at(first);
		const bool vector = isVectorRegister(first);
		std::optional<PhysReg> second;
		if (secondIndex)
			second = frame.savedRegisters[*secondIndex];
		groups.push_back(SavedRegisterGroup{first, second, offset, vector});
	};
	auto canPair = [&](std::size_t firstIndex, std::size_t secondIndex) {
		const PhysReg first = frame.savedRegisters[firstIndex];
		const PhysReg second = frame.savedRegisters[secondIndex];
		const std::int64_t offset = frame.savedRegisterOffsets.at(first);
		if (isVectorRegister(first) != isVectorRegister(second) ||
		    frame.savedRegisterOffsets.at(second) != offset + 8)
			return false;
		// Prefer two direct scalar accesses once a pair offset is out of range.
		// A far pair is kept together so one SP adjustment serves both
		// registers.
		return isPairOffsetEncodable(offset, 8) ||
		       !isScaledOffsetEncodable(offset, 8) ||
		       !isScaledOffsetEncodable(offset + 8, 8);
	};

	if (order == SavedRegisterOrder::Save) {
		for (std::size_t i = 0; i < frame.savedRegisters.size();) {
			const bool pair =
			    i + 1 < frame.savedRegisters.size() && canPair(i, i + 1);
			makeGroup(i,
			          pair ? std::optional<std::size_t>(i + 1) : std::nullopt);
			i += pair ? 2 : 1;
		}
	} else {
		for (std::size_t i = frame.savedRegisters.size(); i > 0;) {
			const bool pair = i >= 2 && canPair(i - 2, i - 1);
			if (pair) {
				makeGroup(i - 2, i - 1);
				i -= 2;
			} else {
				makeGroup(i - 1, std::nullopt);
				--i;
			}
		}
	}
	return groups;
}

void insertSavedRegisterAccess(InstrList &instructions, InstrPosition position,
                               const SavedRegisterGroup &group,
                               MachineMemOperand::Access accessKind) {
	const bool load = accessKind == MachineMemOperand::Access::Load;
	const bool pair = group.second.has_value();
	const bool encodable = pair ? isPairOffsetEncodable(group.offset, 8)
	                            : isScaledOffsetEncodable(group.offset, 8);
	std::uint64_t adjustment = 0;
	std::int64_t memoryOffset = group.offset;
	if (!encodable) {
		if (group.offset < 0)
			throw std::logic_error("negative callee-save offset");
		adjustment = static_cast<std::uint64_t>(group.offset) /
		             kStackAlignment * kStackAlignment;
		memoryOffset -= static_cast<std::int64_t>(adjustment);
		insertStackAdjustment(instructions, position,
		                      StackAdjustment::Deallocate, adjustment,
		                      kMaxAlignedAddSubImmediate);
	}

	const RegClass regClass =
	    group.vector ? RegClass::NEON128 : RegClass::GPR64;
	Opcode opcode;
	if (pair)
		opcode = load ? (group.vector ? Opcode::LDPDi : Opcode::LDPXi)
		              : (group.vector ? Opcode::STPDi : Opcode::STPXi);
	else
		opcode = load ? (group.vector ? Opcode::LDRDui : Opcode::LDRXui)
		              : (group.vector ? Opcode::STRDui : Opcode::STRXui);
	MachineInstr access(opcode);
	access.addOperand(phys(group.first, regClass, load));
	if (pair)
		access.addOperand(phys(*group.second, regClass, load));
	access.addOperand(phys(PhysReg::SP, RegClass::GPR64))
	    .addOperand(MachineOperand::immediate(memoryOffset));
	addMemoryInfo(access, accessKind, pair ? 16 : 8, 8, std::nullopt,
	              group.offset);
	instructions.insert(position, std::move(access));

	if (adjustment)
		insertStackAdjustment(instructions, position, StackAdjustment::Allocate,
		                      adjustment, kMaxAlignedAddSubImmediate);
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
	frame.stackSize = alignTo(cursor, kStackAlignment);
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
					    static_cast<std::uint64_t>(absoluteOffset) /
					    kStackAlignment * kStackAlignment;
					memoryOffset =
					    absoluteOffset - static_cast<std::int64_t>(adjustment);
					base = PhysReg::SP;
					insertStackAdjustment(
					    instructions, it, StackAdjustment::Deallocate,
					    adjustment, kMaxAlignedAddSubImmediate);
					auto restorePosition = std::next(it);
					insertStackAdjustment(instructions, restorePosition,
					                      StackAdjustment::Allocate, adjustment,
					                      kMaxAlignedAddSubImmediate);
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

	insertStackAdjustment(entry->instructions(), insertion,
	                      StackAdjustment::Allocate, frame.stackSize);
	const std::vector<SavedRegisterGroup> savedGroups =
	    groupSavedRegisters(frame, SavedRegisterOrder::Save);
	const std::vector<SavedRegisterGroup> restoredGroups =
	    groupSavedRegisters(frame, SavedRegisterOrder::Restore);
	for (const SavedRegisterGroup &group : savedGroups)
		insertSavedRegisterAccess(entry->instructions(), insertion, group,
		                          MachineMemOperand::Access::Store);

	for (auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end(); ++it) {
			if (it->opcode() != Opcode::RET && it->opcode() != Opcode::TAILCALL)
				continue;
			for (const SavedRegisterGroup &group : restoredGroups)
				insertSavedRegisterAccess(instructions, it, group,
				                          MachineMemOperand::Access::Load);
			insertStackAdjustment(instructions, it, StackAdjustment::Deallocate,
			                      frame.stackSize);
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
