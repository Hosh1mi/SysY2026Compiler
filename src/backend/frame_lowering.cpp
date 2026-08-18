#include "../include/backend/frame_lowering.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <cstdlib>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

constexpr unsigned kStackAlignment = 16;
constexpr unsigned kMaxAddSubImmediate = 4095;
constexpr unsigned kAddSubImmediateShift = 12;
constexpr std::uint64_t kAddSubImmediateScale = std::uint64_t{1}
                                                << kAddSubImmediateShift;

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

std::uint64_t nextAddSubImmediate(std::uint64_t amount) {
	if (amount < kAddSubImmediateScale)
		return std::min<std::uint64_t>(amount, kMaxAddSubImmediate);
	const std::uint64_t shifted = std::min<std::uint64_t>(
	    amount / kAddSubImmediateScale, kMaxAddSubImmediate);
	return shifted * kAddSubImmediateScale;
}

void insertStackAdjustment(InstrList &instructions, InstrPosition position,
                           StackAdjustment direction, std::uint64_t amount) {
	const Opcode opcode = direction == StackAdjustment::Allocate
	                          ? Opcode::SUBSPri
	                          : Opcode::ADDSPri;
	while (amount) {
		const std::uint64_t chunk = nextAddSubImmediate(amount);
		MachineInstr adjust(opcode);
		adjust.addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
		    .addOperand(phys(PhysReg::SP, RegClass::GPR64))
		    .addOperand(
		        MachineOperand::immediate(static_cast<std::int64_t>(chunk)));
		instructions.insert(position, std::move(adjust));
		amount -= chunk;
	}
}

std::int64_t resolvedFrameOffset(const MachineFrameInfo &frame,
                                 const StackObject &object) {
	if (!object.fixed)
		return object.offset;
	// SP remains stable throughout the body.  Fixed objects therefore sit
	// above the local frame and the optional return-address record.
	return static_cast<std::int64_t>(frame.stackSize) +
	       (frame.hasCalls ? kStackAlignment : 0) + object.offset;
}

struct SavedRegisterGroup {
	PhysReg first;
	std::optional<PhysReg> second;
	int offset;
	bool vector;
};

bool canPairSavedRegisters(const MachineFrameInfo &frame,
                           std::size_t firstIndex,
                           std::size_t secondIndex) {
	const PhysReg first = frame.savedRegisters[firstIndex];
	const PhysReg second = frame.savedRegisters[secondIndex];
	const std::int64_t offset = frame.savedRegisterOffsets.at(first);
	if (isVectorRegister(first) != isVectorRegister(second) ||
	    frame.savedRegisterOffsets.at(second) != offset + 8)
		return false;
	return isPairOffsetEncodable(offset, 8) ||
	       !isScaledOffsetEncodable(offset, 8) ||
	       !isScaledOffsetEncodable(offset + 8, 8);
}

std::vector<SavedRegisterGroup>
groupSavedRegisters(const MachineFrameInfo &frame, SavedRegisterOrder order) {
	std::vector<SavedRegisterGroup> groups;
	if (order == SavedRegisterOrder::Save) {
		for (std::size_t i = 0; i < frame.savedRegisters.size();) {
			const bool pair = i + 1 < frame.savedRegisters.size() &&
			                  canPairSavedRegisters(frame, i, i + 1);
			const PhysReg first = frame.savedRegisters[i];
			std::optional<PhysReg> second;
			if (pair)
				second = frame.savedRegisters[i + 1];
			groups.push_back(SavedRegisterGroup{
			    first, second, frame.savedRegisterOffsets.at(first),
			    isVectorRegister(first)});
			i += pair ? 2 : 1;
		}
	} else {
		for (std::size_t i = frame.savedRegisters.size(); i > 0;) {
			const bool pair =
			    i >= 2 && canPairSavedRegisters(frame, i - 2, i - 1);
			const std::size_t firstIndex = pair ? i - 2 : i - 1;
			const PhysReg first = frame.savedRegisters[firstIndex];
			std::optional<PhysReg> second;
			if (pair)
				second = frame.savedRegisters[i - 1];
			groups.push_back(SavedRegisterGroup{
			    first, second, frame.savedRegisterOffsets.at(first),
			    isVectorRegister(first)});
			i -= pair ? 2 : 1;
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
			std::abort();
		adjustment = static_cast<std::uint64_t>(group.offset) /
		             kStackAlignment * kStackAlignment;
		memoryOffset -= static_cast<std::int64_t>(adjustment);
		insertStackAdjustment(instructions, position,
		                      StackAdjustment::Deallocate, adjustment);
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
		                      adjustment);
}

} // namespace

void AArch64FrameLowering::determineCalleeSaves(
    MachineFunction &function) {
	std::unordered_set<PhysReg> used;
	for (const auto &block : function.blocks())
		for (const MachineInstr &instruction : block->instructions())
			for (const MachineOperand &operand : instruction.operands())
				if (operand.isPhysicalRegister() &&
				    RegisterInfo::isCalleeSaved(operand.physicalRegister()))
					used.insert(operand.physicalRegister());

	auto &saved = function.frameInfo().savedRegisters;
	saved.assign(used.begin(), used.end());
	std::sort(saved.begin(), saved.end());
}

void AArch64FrameLowering::layoutFrame(MachineFunction &function) {
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
	for (int pass = 0; pass < 2; ++pass) {
		const bool spills = pass == 0;
		for (StackObject &object : frame.objects()) {
			if (object.fixed || object.spill != spills)
				continue;
			cursor = alignTo(cursor, object.alignment);
			object.offset = static_cast<int>(cursor);
			cursor += object.size;
		}
	}
	// Keep allocator spill slots in the low, directly encodable SP window.
	// Large local arrays are addressed through LEA_FRAME values and can sit
	// above them without adding work to each dynamic spill/reload.
	frame.stackSize = alignTo(cursor, kStackAlignment);
}

void AArch64FrameLowering::eliminateFrameIndices(
    MachineFunction &function) {
	MachineFrameInfo &frame = function.frameInfo();
	for (auto &owned : function.blocks()) {
		auto &instructions = owned->instructions();
		for (auto it = instructions.begin(); it != instructions.end();) {
			if (it->opcode() == Opcode::ADJCALLSTACKDOWN ||
			    it->opcode() == Opcode::ADJCALLSTACKUP) {
				if (it->operands().size() != 1 ||
				    it->operands()[0].kind() != MachineOperand::Kind::Immediate)
					std::abort();
				it = instructions.erase(it);
				continue;
			}
			if (it->opcode() == Opcode::LEA_FRAME) {
				if (it->operands().size() != 2 ||
				    !it->operands()[0].isPhysicalRegister() ||
				    it->operands()[1].kind() !=
				        MachineOperand::Kind::FrameIndex)
					std::abort();
				const StackObject &object =
				    frame.getObject(it->operands()[1].frameIndex());
				const PhysReg base = PhysReg::SP;
				std::int64_t offset = resolvedFrameOffset(frame, object);
				MachineOperand destination = it->operands()[0];
				if (offset < 0)
					std::abort();
				std::uint64_t amount =
				    nextAddSubImmediate(static_cast<std::uint64_t>(offset));
				it->operands().clear();
				if (amount == 0) {
					it->setOpcode(Opcode::MOVXrr);
					it->addOperand(destination)
					    .addOperand(phys(base, RegClass::GPR64));
				} else {
					it->setOpcode(Opcode::ADDXri);
					it->addOperand(destination)
					    .addOperand(phys(base, RegClass::GPR64))
					    .addOperand(MachineOperand::immediate(
					        static_cast<std::int64_t>(amount)));
				}
				offset -= amount;
				auto insertion = std::next(it);
				while (offset) {
					amount =
					    nextAddSubImmediate(static_cast<std::uint64_t>(offset));
					MachineInstr add(Opcode::ADDXri);
					add.addOperand(destination)
					    .addOperand(MachineOperand::physReg(
					        destination.physicalRegister(), RegClass::GPR64))
					    .addOperand(MachineOperand::immediate(
					        static_cast<std::int64_t>(amount)));
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
					std::abort();
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
				const std::int64_t absoluteOffset =
				    resolvedFrameOffset(frame, object);
				std::int64_t memoryOffset = absoluteOffset;
				const PhysReg base = PhysReg::SP;
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
					insertStackAdjustment(instructions, it,
					                      StackAdjustment::Deallocate,
					                      adjustment);
					auto restorePosition = std::next(it);
					insertStackAdjustment(instructions, restorePosition,
					                      StackAdjustment::Allocate,
					                      adjustment);
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
    MachineFunction &function) {
	MachineFrameInfo &frame = function.frameInfo();
	if (frame.stackSize == 0 && !frame.hasCalls)
		return;

	MachineBasicBlock *entry = function.entryBlock();
	if (!entry)
		std::abort();
	auto insertion = entry->instructions().begin();

	if (frame.hasCalls) {
		MachineInstr push(Opcode::STPXpre);
		push.addOperand(phys(PhysReg::XZR, RegClass::GPR64))
		    .addOperand(phys(PhysReg::X30, RegClass::GPR64))
		    .addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
		    .addOperand(MachineOperand::immediate(-16));
		addMemoryInfo(push, MachineMemOperand::Access::Store, 16, 16,
		              std::nullopt, -16);
		entry->instructions().insert(insertion, std::move(push));
	}

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
			if (frame.hasCalls) {
				MachineInstr pop(Opcode::LDPXpost);
				pop.addOperand(phys(PhysReg::XZR, RegClass::GPR64, true))
				    .addOperand(phys(PhysReg::X30, RegClass::GPR64, true))
				    .addOperand(phys(PhysReg::SP, RegClass::GPR64, true))
				    .addOperand(MachineOperand::immediate(16));
				addMemoryInfo(pop, MachineMemOperand::Access::Load, 16, 16,
				              std::nullopt, 0);
				instructions.insert(it, std::move(pop));
			}
		}
	}
}

bool AArch64FrameLowering::run(MachineFunction &function) {
	determineCalleeSaves(function);
	layoutFrame(function);
	eliminateFrameIndices(function);
	insertPrologueEpilogues(function);
	function.setProperty(MachineProperty::FrameFinalized);
	return true;
}

} // namespace backend::aarch64
