#include "backend/isel.hpp"
#include "backend/aarch64_isel.hpp"
#include "backend/constant_division.hpp"
#include "backend/vector_immediate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

bool isPowerOfTwo(unsigned value) {
	return value && (value & (value - 1)) == 0;
}

unsigned log2Exact(unsigned value) {
	unsigned result = 0;
	while (value > 1) {
		value >>= 1;
		++result;
	}
	return result;
}

bool maskEquals(const std::array<int, 4> &mask,
                std::initializer_list<int> expected) {
	unsigned index = 0;
	for (int value : expected) {
		if (index >= mask.size() || mask[index] != value)
			return false;
		++index;
	}
	return index == mask.size();
}

RegisterMask callPreservedMask() {
	RegisterMask mask;
	mask.setPreserved(PhysReg::SP);
	mask.setPreserved(PhysReg::X18);
	mask.setPreserved(PhysReg::X19);
	mask.setPreserved(PhysReg::X20);
	mask.setPreserved(PhysReg::X21);
	mask.setPreserved(PhysReg::X22);
	mask.setPreserved(PhysReg::X23);
	mask.setPreserved(PhysReg::X24);
	mask.setPreserved(PhysReg::X25);
	mask.setPreserved(PhysReg::X26);
	mask.setPreserved(PhysReg::X27);
	mask.setPreserved(PhysReg::X28);
	mask.setPreserved(PhysReg::X29);
	for (PhysReg reg : RegisterInfo::calleeSaved(RegClass::NEON128))
		mask.setPreserved(reg);
	return mask;
}

class InstructionSelectionContext {
public:
	InstructionSelectionContext(MachineFunction &function,
	                            std::unordered_map<SDNode *, VReg> &results)
	    : registerInfo_(function.registerInfo()), results_(results) {}

	VReg getResult(SDValue value) const {
		auto found = results_.find(value.node);
		if (found == results_.end())
			throw std::logic_error("DAG operand has no selected register");
		return found->second;
	}

	RegClass getValueClass(SDValue value) const {
		return registerInfo_.get(getResult(value)).regClass;
	}

	MachineOperand makeUse(SDValue value) const {
		VReg reg = getResult(value);
		return MachineOperand::vreg(reg, registerInfo_.get(reg).regClass);
	}

	MachineOperand makeDef(SDNode &node) const {
		VReg reg = results_.at(&node);
		return MachineOperand::vreg(reg, registerInfo_.get(reg).regClass, true);
	}

	MachineInstr &emit(MachineBasicBlock &block, MachineInstr instruction,
	                   SDNode *definition = nullptr) {
		MachineInstr &inserted = block.append(std::move(instruction));
		if (definition && results_.count(definition))
			registerInfo_.setDefinition(results_.at(definition), &inserted);
		return inserted;
	}

	VReg createVReg(ValueType type) {
		return registerInfo_.createVirtualRegister(
		    RegisterInfo::classForType(type), type);
	}

	MachineOperand emitTemporary(MachineBasicBlock &block,
	                             MachineInstr instruction, VReg reg) {
		MachineInstr &inserted = emit(block, std::move(instruction));
		registerInfo_.setDefinition(reg, &inserted);
		return MachineOperand::vreg(reg, registerInfo_.get(reg).regClass);
	}

	MachineOperand
	emitUnary(MachineBasicBlock &block, Opcode opcode, ValueType type,
	          MachineOperand source,
	          std::optional<std::int64_t> immediate = std::nullopt) {
		VReg reg = createVReg(type);
		MachineInstr instruction(opcode);
		instruction
		    .addOperand(MachineOperand::vreg(
		        reg, registerInfo_.get(reg).regClass, true))
		    .addOperand(std::move(source));
		if (immediate)
			instruction.addOperand(MachineOperand::immediate(*immediate));
		return emitTemporary(block, std::move(instruction), reg);
	}

	MachineOperand emitBinary(MachineBasicBlock &block, Opcode opcode,
	                          ValueType type, MachineOperand lhs,
	                          MachineOperand rhs) {
		VReg reg = createVReg(type);
		MachineInstr instruction(opcode);
		instruction
		    .addOperand(MachineOperand::vreg(
		        reg, registerInfo_.get(reg).regClass, true))
		    .addOperand(std::move(lhs))
		    .addOperand(std::move(rhs));
		return emitTemporary(block, std::move(instruction), reg);
	}

	void emitFinal(MachineBasicBlock &block, MachineInstr instruction,
	               const MachineOperand &destination, SDNode *definition) {
		MachineInstr &inserted =
		    emit(block, std::move(instruction), definition);
		if (!definition && destination.isVirtualRegister())
			registerInfo_.setDefinition(destination.virtualRegister(),
			                            &inserted);
	}

private:
	MachineRegisterInfo &registerInfo_;
	std::unordered_map<SDNode *, VReg> &results_;
};

bool emitGeneratedPattern(
    InstructionSelectionContext &selection, MachineBasicBlock &block,
    SDNode &node,
    const std::unordered_map<BasicBlock *, MachineBasicBlock *> &blocks,
    bool directGlobal = false) {
	const generated::SelectionPattern *pattern =
	    generated::matchPattern(node, directGlobal);
	if (!pattern)
		return false;

	MachineInstr instruction(pattern->opcode);
	for (unsigned index = 0; index < pattern->operandCount; ++index) {
		const generated::PatternOperand operand = pattern->operands[index];
		switch (operand.kind) {
		case generated::PatternOperandKind::Definition:
			instruction.addOperand(selection.makeDef(node));
			break;
		case generated::PatternOperandKind::Use:
			instruction.addOperand(
			    selection.makeUse(node.operands().at(operand.index)));
			break;
		case generated::PatternOperandKind::OperandInteger: {
			SDNode *constant = node.operands().at(operand.index).node;
			if (!constant || constant->opcode() != SDOpcode::Constant)
				throw std::logic_error(
				    "generated immediate operand is not constant");
			instruction.addOperand(
			    MachineOperand::immediate(constant->integer));
			break;
		}
		case generated::PatternOperandKind::NodeInteger:
			instruction.addOperand(MachineOperand::immediate(node.integer));
			break;
		case generated::PatternOperandKind::FrameIndex:
			instruction.addOperand(MachineOperand::frameIndex(node.index));
			break;
		case generated::PatternOperandKind::NodeGlobal:
			instruction.addOperand(MachineOperand::global(node.symbol));
			break;
		case generated::PatternOperandKind::Block:
			instruction.addOperand(MachineOperand::block(
			    blocks.at(node.incomingBlocks.at(operand.index))));
			break;
		case generated::PatternOperandKind::OperandGlobal: {
			SDNode *address = node.operands().at(operand.index).node;
			if (!address || address->opcode() != SDOpcode::GlobalAddress)
				throw std::logic_error(
				    "generated global operand has no symbol");
			instruction.addOperand(MachineOperand::global(address->symbol));
			break;
		}
		case generated::PatternOperandKind::Zero:
			instruction.addOperand(MachineOperand::immediate(0));
			break;
		}
		MachineOperand &emitted = instruction.operands().back();
		emitted.tiedTo = operand.tiedTo;
		emitted.isEarlyClobber = operand.earlyClobber;
	}
	if (pattern->memory != generated::PatternMemoryAction::None) {
		const auto access =
		    pattern->memory == generated::PatternMemoryAction::Load
		        ? MachineMemOperand::Access::Load
		        : MachineMemOperand::Access::Store;
		instruction.addMemoryOperand(
		    MachineMemOperand{access, node.memorySize, node.alignment,
		                      node.origin, std::nullopt, 0, false});
	}
	selection.emit(block, std::move(instruction),
	               node.resultTypes().empty() ||
	                       node.resultTypes().front() == ValueType::Invalid
	                   ? nullptr
	                   : &node);
	return true;
}

} // namespace

std::unique_ptr<MachineFunction>
AArch64InstructionSelector::select(FunctionDAG &functionDAG) const {
	if (!functionDAG.legalized)
		throw std::logic_error(
		    "instruction selection requires a legalized SelectionDAG");
	auto machineFunction =
	    std::make_unique<MachineFunction>(functionDAG.function->name_);
	machineFunction->setProperty(MachineProperty::IsSSA);
	machineFunction->setProperty(MachineProperty::Legalized);
	machineFunction->setProperty(MachineProperty::Selected);

	bool hasPHIs = false;
	for (BasicBlock *block : functionDAG.blockOrder)
		for (const auto &node : functionDAG.blocks.at(block)->nodes())
			hasPHIs |= node->opcode() == SDOpcode::Phi;
	if (hasPHIs)
		machineFunction->setProperty(MachineProperty::HasPHIs);

	std::unordered_map<BasicBlock *, MachineBasicBlock *> blocks;
	for (BasicBlock *block : functionDAG.blockOrder)
		blocks.emplace(block, &machineFunction->createBlock(
		                          block->name_.empty() ? "bb" : block->name_));
	for (BasicBlock *block : functionDAG.blockOrder)
		for (BasicBlock *successor : block->succ_bbs_)
			blocks.at(block)->addSuccessor(blocks.at(successor));

	auto &registerInfo = machineFunction->registerInfo();
	std::unordered_map<SDNode *, VReg> results;
	std::unordered_map<SDNode *, bool> directGlobalMemory;
	for (BasicBlock *block : functionDAG.blockOrder)
		for (const auto &owned : functionDAG.blocks.at(block)->nodes())
			if (owned->opcode() == SDOpcode::GlobalAddress)
				directGlobalMemory.emplace(owned.get(), true);
	for (BasicBlock *block : functionDAG.blockOrder) {
		for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
			SDNode &user = *owned;
			for (unsigned index = 0; index < user.operands().size(); ++index) {
				SDNode *operand = user.operands()[index].node;
				auto global = directGlobalMemory.find(operand);
				if (global == directGlobalMemory.end())
					continue;
				if (generated::dagAddressOperand(user.opcode()) !=
				    static_cast<int>(index))
					global->second = false;
			}
		}
	}
	// Allocate result identities before emission so loop PHIs can reference
	// backedge definitions that are emitted later.
	for (BasicBlock *block : functionDAG.blockOrder) {
		for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
			SDNode &node = *owned;
			if (node.opcode() == SDOpcode::EntryToken ||
			    node.opcode() == SDOpcode::Invalid ||
			    node.resultTypes().empty() ||
			    node.resultTypes().front() == ValueType::Invalid)
				continue;
			ValueType type = node.resultTypes().front();
			RegClass regClass = RegisterInfo::classForType(type);
			if (regClass == RegClass::Invalid)
				throw std::logic_error("selected value has no register class");
			results.emplace(&node,
			                registerInfo.createVirtualRegister(regClass, type));
		}
	}

	InstructionSelectionContext selection(*machineFunction, results);
	auto emitSignedConstantDivision =
	    [&](MachineBasicBlock &block, MachineOperand destination,
	        MachineOperand numerator, std::int32_t divisor,
	        SDNode *definition = nullptr) -> bool {
		division::SignedDivisorInfo info =
		    division::analyzeSignedDivisor(divisor);
		if (!info.reducible)
			return false;

		if (divisor == 1) {
			MachineInstr instruction(Opcode::COPY);
			instruction.addOperand(destination).addOperand(numerator);
			selection.emitFinal(block, std::move(instruction), destination,
			                    definition);
		} else if (divisor == -1) {
			MachineInstr instruction(Opcode::NEGW);
			instruction.addOperand(destination).addOperand(numerator);
			selection.emitFinal(block, std::move(instruction), destination,
			                    definition);
		} else if (info.powerOfTwo) {
			MachineOperand adjusted;
			if (info.shift == 1) {
				VReg adjustedReg = selection.createVReg(ValueType::I32);
				MachineInstr addBias(Opcode::ADDWrs);
				addBias
				    .addOperand(MachineOperand::vreg(adjustedReg,
				                                     RegClass::GPR32, true))
				    .addOperand(numerator)
				    .addOperand(numerator)
				    .addOperand(MachineOperand::immediate(31));
				adjusted = selection.emitTemporary(block, std::move(addBias),
				                                   adjustedReg);
			} else {
				MachineOperand sign = selection.emitUnary(
				    block, Opcode::ASRWri, ValueType::I32, numerator, 31);
				MachineOperand bias =
				    selection.emitUnary(block, Opcode::LSRWri, ValueType::I32,
				                        sign, 32 - info.shift);
				adjusted = selection.emitBinary(
				    block, Opcode::ADDWrr, ValueType::I32, numerator, bias);
			}
			if (divisor < 0) {
				MachineOperand quotient =
				    selection.emitUnary(block, Opcode::ASRWri, ValueType::I32,
				                        adjusted, info.shift);
				MachineInstr negate(Opcode::NEGW);
				negate.addOperand(destination).addOperand(quotient);
				selection.emitFinal(block, std::move(negate), destination,
				                    definition);
			} else {
				MachineInstr shift(Opcode::ASRWri);
				shift.addOperand(destination)
				    .addOperand(adjusted)
				    .addOperand(MachineOperand::immediate(info.shift));
				selection.emitFinal(block, std::move(shift), destination,
				                    definition);
			}
		} else {
			division::MagicNumber magic = division::computeSignedMagic(divisor);
			VReg multiplierReg = selection.createVReg(ValueType::I32);
			MachineInstr materialize(Opcode::MOVi32);
			materialize
			    .addOperand(
			        MachineOperand::vreg(multiplierReg, RegClass::GPR32, true))
			    .addOperand(MachineOperand::immediate(magic.multiplier));
			MachineOperand multiplier = selection.emitTemporary(
			    block, std::move(materialize), multiplierReg);

			MachineOperand product = selection.emitBinary(
			    block, Opcode::SMULLXrr, ValueType::Ptr, numerator, multiplier);
			if (magic.strategy == division::MagicStrategy::MultiplyShift) {
				MachineOperand shifted =
				    selection.emitUnary(block, Opcode::ASRXri, ValueType::Ptr,
				                        product, 32 + magic.shift);
				MachineInstr add(Opcode::ADDWrsX);
				add.addOperand(destination)
				    .addOperand(shifted)
				    .addOperand(shifted)
				    .addOperand(MachineOperand::immediate(31));
				selection.emitFinal(block, std::move(add), destination,
				                    definition);
				return true;
			}
			MachineOperand highX = selection.emitUnary(
			    block, Opcode::ASRXri, ValueType::Ptr, product, 32);
			MachineOperand high = selection.emitUnary(block, Opcode::COPYXtoW,
			                                          ValueType::I32, highX);
			if (magic.strategy == division::MagicStrategy::MultiplyAddShift)
				high = selection.emitBinary(block, Opcode::ADDWrr,
				                            ValueType::I32, high, numerator);
			else
				high = selection.emitBinary(block, Opcode::SUBWrr,
				                            ValueType::I32, high, numerator);
			if (magic.shift)
				high = selection.emitUnary(block, Opcode::ASRWri,
				                           ValueType::I32, high, magic.shift);
			// The final round-toward-zero correction depends on the sign of
			// the approximate quotient, not the dividend.  They have the
			// same sign for positive divisors, which can hide this
			// distinction; for a negative divisor the quotient sign is
			// reversed.
			MachineInstr add(Opcode::ADDWrs);
			add.addOperand(destination)
			    .addOperand(high)
			    .addOperand(high)
			    .addOperand(MachineOperand::immediate(31));
			selection.emitFinal(block, std::move(add), destination, definition);
		}
		return true;
	};
	auto emitVectorConstant = [&](MachineBasicBlock &block,
	                              MachineOperand destination,
	                              const std::array<std::uint32_t, 4> &lanes,
	                              SDNode *definition = nullptr) {
		bool allEqual = lanes[0] == lanes[1] && lanes[1] == lanes[2] &&
		                lanes[2] == lanes[3];
		if (allEqual) {
			if (auto immediate = classifyNeonSplatImmediate(lanes[0])) {
				selection.emitFinal(
				    block, makeNeonSplatImmediate(*immediate, destination),
				    destination, definition);
				return;
			}

			// Arbitrary identical lanes: scalar materialize + dup.
			VReg scalar = selection.createVReg(ValueType::I32);
			MachineInstr materialize(Opcode::MOVi32);
			materialize
			    .addOperand(MachineOperand::vreg(scalar, RegClass::GPR32, true))
			    .addOperand(MachineOperand::immediate(
			        static_cast<std::int64_t>(lanes[0])));
			MachineInstr &scalarDefinition =
			    selection.emit(block, std::move(materialize));
			registerInfo.setDefinition(scalar, &scalarDefinition);
			MachineInstr duplicate(Opcode::DUPv4i32);
			duplicate.addOperand(destination)
			    .addOperand(MachineOperand::vreg(scalar, RegClass::GPR32));
			selection.emitFinal(block, std::move(duplicate), destination,
			                    definition);
			return;
		}

		// Non-splat 128-bit immediates are cheaper from a pool load than
		// from a chain of per-lane inserts.
		const std::string &label =
		    machineFunction->getOrCreateVectorConstant(lanes);
		VReg page = selection.createVReg(ValueType::Ptr);
		MachineInstr adrp(Opcode::ADRP);
		adrp.addOperand(MachineOperand::vreg(page, RegClass::GPR64, true))
		    .addOperand(MachineOperand::global(label));
		MachineInstr &pageDefinition = selection.emit(block, std::move(adrp));
		registerInfo.setDefinition(page, &pageDefinition);
		MachineInstr load(Opcode::LDRQlo);
		load.addOperand(destination)
		    .addOperand(MachineOperand::vreg(page, RegClass::GPR64))
		    .addOperand(MachineOperand::global(label));
		load.addMemoryOperand(MachineMemOperand{MachineMemOperand::Access::Load,
		                                        16, 16, nullptr, std::nullopt,
		                                        0, false});
		selection.emitFinal(block, std::move(load), destination, definition);
	};
	unsigned nextParallelCopyGroup = 1;
	const unsigned entryArgumentCopyGroup = nextParallelCopyGroup++;

	// FrameIndex nodes carry stable indices assigned by the DAG builder.
	std::vector<SDNode *> frameNodes;
	for (BasicBlock *block : functionDAG.blockOrder)
		for (const auto &node : functionDAG.blocks.at(block)->nodes())
			if (node->opcode() == SDOpcode::FrameIndex)
				frameNodes.push_back(node.get());
	std::sort(frameNodes.begin(), frameNodes.end(),
	          [](SDNode *lhs, SDNode *rhs) { return lhs->index < rhs->index; });
	for (SDNode *node : frameNodes) {
		int index = machineFunction->frameInfo().createStackObject(
		    node->memorySize, node->alignment, false);
		if (index != static_cast<int>(node->index))
			throw std::logic_error("DAG frame-index numbering diverged");
	}

	// Source argument indices use independent GPR and SIMD register banks.
	std::vector<unsigned> argumentBankIndex(
	    functionDAG.function->arguments_.size());
	std::vector<bool> argumentIsFloat(functionDAG.function->arguments_.size());
	std::vector<int> argumentStackOffset(
	    functionDAG.function->arguments_.size(), -1);
	unsigned integerArguments = 0;
	unsigned floatArguments = 0;
	unsigned incomingStackSize = 0;
	for (Argument *argument : functionDAG.function->arguments_) {
		ValueType type = SelectionDAGBuilder::valueType(argument->type_);
		bool isFloat = type == ValueType::F32 || type == ValueType::V4F32 ||
		               type == ValueType::V4I32;
		argumentIsFloat[argument->arg_no_] = isFloat;
		argumentBankIndex[argument->arg_no_] =
		    isFloat ? floatArguments++ : integerArguments++;
		if (argumentBankIndex[argument->arg_no_] >= 8) {
			unsigned alignment =
			    type == ValueType::V4I32 || type == ValueType::V4F32 ? 16U : 8U;
			incomingStackSize =
			    (incomingStackSize + alignment - 1) / alignment * alignment;
			argumentStackOffset[argument->arg_no_] =
			    static_cast<int>(incomingStackSize);
			incomingStackSize += alignment;
		}
	}

	std::optional<int> dynamicExtractSlot;
	for (BasicBlock *sourceBlock : functionDAG.blockOrder) {
		MachineBasicBlock &block = *blocks.at(sourceBlock);
		std::optional<VReg> dynamicExtractBase;
		for (const auto &owned : functionDAG.blocks.at(sourceBlock)->nodes()) {
			SDNode &node = *owned;
			const generated::SelectionMode selectionMode =
			    generated::selectionMode(node.opcode());
			if (selectionMode == generated::SelectionMode::Ignore)
				continue;
			if (selectionMode == generated::SelectionMode::Generated) {
				bool directGlobal = false;
				const int addressIndex =
				    generated::dagAddressOperand(node.opcode());
				if (addressIndex >= 0) {
					SDNode *address = node.operands().at(addressIndex).node;
					directGlobal =
					    address &&
					    address->opcode() == SDOpcode::GlobalAddress &&
					    directGlobalMemory.at(address);
				}
				if (!emitGeneratedPattern(selection, block, node, blocks,
				                          directGlobal))
					throw std::logic_error(
					    std::string("no generated pattern for ") +
					    sdOpcodeName(node.opcode()));
				continue;
			}
			switch (generated::customSelector(node.opcode())) {
			case generated::CustomSelector::None:
				throw std::logic_error(
				    "ignored DAG opcode reached custom selection");
			case generated::CustomSelector::Argument: {
				ValueType type = node.resultTypes().front();
				RegClass regClass = RegisterInfo::classForType(type);
				unsigned bank = argumentBankIndex.at(node.index);
				if (bank < 8) {
					PhysReg phys =
					    argumentIsFloat.at(node.index)
					        ? RegisterInfo::vectorArgumentRegister(bank)
					        : RegisterInfo::integerArgumentRegister(bank);
					MachineInstr copy(Opcode::COPY);
					copy.parallelCopyGroup = entryArgumentCopyGroup;
					copy.addOperand(selection.makeDef(node))
					    .addOperand(MachineOperand::physReg(phys, regClass));
					selection.emit(block, std::move(copy), &node);
				} else {
					int fixed = machineFunction->frameInfo().createFixedObject(
					    regClass == RegClass::NEON128 ? 16 : 8,
					    argumentStackOffset.at(node.index),
					    regClass == RegClass::NEON128 ? 16 : 8);
					// Preserve the fixed incoming-frame reference through
					// register allocation.  Frame lowering can then select
					// the x29-relative scaled addressing mode directly,
					// instead of materializing an address for every stack
					// argument before allocation.
					MachineInstr load(Opcode::SPILL_LOAD);
					load.addOperand(selection.makeDef(node))
					    .addOperand(MachineOperand::frameIndex(fixed));
					load.addMemoryOperand(
					    MachineMemOperand{MachineMemOperand::Access::Load,
					                      regClass == RegClass::NEON128 ? 16U
					                      : regClass == RegClass::GPR64 ? 8U
					                                                    : 4U,
					                      regClass == RegClass::NEON128 ? 16U
					                      : regClass == RegClass::GPR64 ? 8U
					                                                    : 4U,
					                      nullptr, fixed, 0, false});
					selection.emit(block, std::move(load), &node);
				}
				break;
			}
			case generated::CustomSelector::Constant: {
				ValueType type = node.resultTypes().front();
				if (type == ValueType::V4I32 || type == ValueType::V4F32) {
					std::array<std::uint32_t, 4> lanes{};
					for (unsigned lane = 0; lane < lanes.size(); ++lane)
						if (lane < node.shuffleMask.size())
							lanes[lane] = static_cast<std::uint32_t>(
							    node.shuffleMask[lane]);
					emitVectorConstant(block, selection.makeDef(node), lanes,
					                   &node);
					break;
				}
				if (!emitGeneratedPattern(selection, block, node, blocks))
					throw std::logic_error(
					    "no generated scalar constant pattern");
				break;
			}
			case generated::CustomSelector::FPConstant: {
				VReg bits = selection.createVReg(ValueType::I32);
				MachineInstr materialize(Opcode::MOVi32);
				materialize
				    .addOperand(
				        MachineOperand::vreg(bits, RegClass::GPR32, true))
				    .addOperand(MachineOperand::immediate(node.floatingBits));
				MachineInstr &bitsDefinition =
				    selection.emit(block, std::move(materialize));
				registerInfo.setDefinition(bits, &bitsDefinition);
				MachineInstr move(Opcode::FMOVSW);
				move.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::vreg(bits, RegClass::GPR32));
				selection.emit(block, std::move(move), &node);
				break;
			}
			case generated::CustomSelector::GlobalAddress: {
				if (directGlobalMemory.at(&node)) {
					if (!emitGeneratedPattern(selection, block, node, blocks,
					                          true))
						throw std::logic_error(
						    "no generated direct-global pattern");
					break;
				}
				VReg page = selection.createVReg(ValueType::Ptr);
				MachineInstr adrp(Opcode::ADRP);
				adrp.addOperand(
				        MachineOperand::vreg(page, RegClass::GPR64, true))
				    .addOperand(MachineOperand::global(node.symbol));
				MachineInstr &pageDef = selection.emit(block, std::move(adrp));
				registerInfo.setDefinition(page, &pageDef);
				MachineInstr add(Opcode::ADDlow);
				add.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::vreg(page, RegClass::GPR64))
				    .addOperand(MachineOperand::global(node.symbol));
				selection.emit(block, std::move(add), &node);
				break;
			}
			case generated::CustomSelector::Add:
			case generated::CustomSelector::Sub:
			case generated::CustomSelector::Mul:
			case generated::CustomSelector::SDiv:
			case generated::CustomSelector::UDiv:
			case generated::CustomSelector::And:
			case generated::CustomSelector::Or:
			case generated::CustomSelector::Xor:
			case generated::CustomSelector::Shl:
			case generated::CustomSelector::LShr:
			case generated::CustomSelector::AShr:
			case generated::CustomSelector::FAdd:
			case generated::CustomSelector::FSub:
			case generated::CustomSelector::FMul:
			case generated::CustomSelector::FDiv: {
				ValueType resultType = node.resultTypes().front();
				bool integerVector = resultType == ValueType::V4I32;
				bool floatingVector = resultType == ValueType::V4F32;
				bool integer64 = resultType == ValueType::I64;

				SDNode *rhs = node.operands()[1].node;
				SDNode *lhs = node.operands()[0].node;
				if (integerVector && node.opcode() == SDOpcode::Sub && lhs &&
				    lhs->opcode() == SDOpcode::Constant &&
				    lhs->shuffleMask.empty()) {
					MachineInstr negate(Opcode::NEGv4i32);
					negate.addOperand(selection.makeDef(node))
					    .addOperand(selection.makeUse(node.operands()[1]));
					selection.emit(block, std::move(negate), &node);
					break;
				}
				if (!integerVector && !floatingVector && !integer64 &&
				    node.opcode() == SDOpcode::Mul) {
					SDNode *constant = nullptr;
					SDValue variable;
					if (rhs && rhs->opcode() == SDOpcode::Constant) {
						constant = rhs;
						variable = node.operands()[0];
					} else if (lhs && lhs->opcode() == SDOpcode::Constant) {
						constant = lhs;
						variable = node.operands()[1];
					}
					if (constant) {
						std::int32_t factor =
						    static_cast<std::int32_t>(constant->integer);
						std::uint32_t magnitude =
						    factor < 0 ? 0U - static_cast<std::uint32_t>(factor)
						               : static_cast<std::uint32_t>(factor);
						Opcode reduced = Opcode::Invalid;
						unsigned shift = 0;
						if (factor == 0)
							reduced = Opcode::MOVi32;
						else if (magnitude == 1)
							reduced = factor < 0 ? Opcode::NEGW : Opcode::COPY;
						else if (isPowerOfTwo(magnitude)) {
							reduced = Opcode::LSLWri;
							shift = log2Exact(magnitude);
						} else if (magnitude > 1 &&
						           isPowerOfTwo(magnitude - 1)) {
							reduced = Opcode::ADDWlsl;
							shift = log2Exact(magnitude - 1);
						}

						if (reduced != Opcode::Invalid) {
							bool needsNegate = factor < 0 && magnitude != 1;
							MachineOperand destination =
							    selection.makeDef(node);
							VReg temporary = 0;
							if (needsNegate) {
								temporary =
								    selection.createVReg(ValueType::I32);
								destination = MachineOperand::vreg(
								    temporary, RegClass::GPR32, true);
							}
							MachineInstr instruction(reduced);
							instruction.addOperand(destination);
							if (factor == 0)
								instruction.addOperand(
								    MachineOperand::immediate(0));
							else {
								instruction.addOperand(
								    selection.makeUse(variable));
								if (reduced == Opcode::LSLWri)
									instruction.addOperand(
									    MachineOperand::immediate(shift));
								else if (reduced == Opcode::ADDWlsl)
									instruction
									    .addOperand(selection.makeUse(variable))
									    .addOperand(
									        MachineOperand::immediate(shift));
							}
							MachineInstr &selected =
							    selection.emit(block, std::move(instruction),
							                   needsNegate ? nullptr : &node);
							if (needsNegate) {
								registerInfo.setDefinition(temporary,
								                           &selected);
								MachineInstr negate(Opcode::NEGW);
								negate.addOperand(selection.makeDef(node))
								    .addOperand(MachineOperand::vreg(
								        temporary, RegClass::GPR32));
								selection.emit(block, std::move(negate), &node);
							}
							break;
						}
					}
				}
				if (!integerVector && !floatingVector && !integer64 &&
				    node.opcode() == SDOpcode::SDiv && rhs &&
				    rhs->opcode() == SDOpcode::Constant &&
				    emitSignedConstantDivision(
				        block, selection.makeDef(node),
				        selection.makeUse(node.operands()[0]),
				        static_cast<std::int32_t>(rhs->integer), &node))
					break;

				if (integerVector && rhs &&
				    (node.opcode() == SDOpcode::Shl ||
				     node.opcode() == SDOpcode::LShr ||
				     node.opcode() == SDOpcode::AShr) &&
				    rhs->opcode() == SDOpcode::Splat &&
				    rhs->operands().size() == 1) {
					SDNode *amount = rhs->operands()[0].node;
					if (amount && amount->opcode() == SDOpcode::Constant) {
						const std::int64_t shift = amount->integer;
						const bool validLeft = node.opcode() == SDOpcode::Shl &&
						                       shift >= 0 && shift < 32;
						const bool validRight =
						    node.opcode() != SDOpcode::Shl && shift > 0 &&
						    shift < 32;
						if (validLeft || validRight) {
							Opcode immediateShift =
							    node.opcode() == SDOpcode::Shl
							        ? Opcode::SHLiv4i32
							    : node.opcode() == SDOpcode::LShr
							        ? Opcode::USHRiv4i32
							        : Opcode::SSHRiv4i32;
							MachineInstr shifted(immediateShift);
							shifted.addOperand(selection.makeDef(node))
							    .addOperand(
							        selection.makeUse(node.operands()[0]))
							    .addOperand(MachineOperand::immediate(shift));
							selection.emit(block, std::move(shifted), &node);
							break;
						}
					}
				}
				if (integerVector && node.opcode() == SDOpcode::Mul) {
					SDNode *splat = nullptr;
					SDValue value;
					if (rhs && rhs->opcode() == SDOpcode::Splat) {
						splat = rhs;
						value = node.operands()[0];
					} else if (lhs && lhs->opcode() == SDOpcode::Splat) {
						splat = lhs;
						value = node.operands()[1];
					}
					SDNode *factor = splat && splat->operands().size() == 1
					                     ? splat->operands()[0].node
					                     : nullptr;
					if (factor && factor->opcode() == SDOpcode::Constant &&
					    factor->integer > 0 && factor->integer <= INT32_MAX &&
					    isPowerOfTwo(
					        static_cast<std::uint32_t>(factor->integer))) {
						MachineInstr shifted(Opcode::SHLiv4i32);
						shifted.addOperand(selection.makeDef(node))
						    .addOperand(selection.makeUse(value))
						    .addOperand(MachineOperand::immediate(log2Exact(
						        static_cast<std::uint32_t>(factor->integer))));
						selection.emit(block, std::move(shifted), &node);
						break;
					}
				}
				if (integerVector && (node.opcode() == SDOpcode::LShr ||
				                      node.opcode() == SDOpcode::AShr)) {
					VReg negated = selection.createVReg(ValueType::V4I32);
					MachineInstr negate(Opcode::NEGv4i32);
					negate
					    .addOperand(MachineOperand::vreg(
					        negated, RegClass::NEON128, true))
					    .addOperand(selection.makeUse(node.operands()[1]));
					MachineInstr &negateDef =
					    selection.emit(block, std::move(negate));
					registerInfo.setDefinition(negated, &negateDef);
					MachineInstr instruction(node.opcode() == SDOpcode::LShr
					                             ? Opcode::USHLv4i32
					                             : Opcode::SSHLv4i32);
					instruction.addOperand(selection.makeDef(node))
					    .addOperand(selection.makeUse(node.operands()[0]));
					instruction.addOperand(
					    MachineOperand::vreg(negated, RegClass::NEON128));
					selection.emit(block, std::move(instruction), &node);
					break;
				}
				if (!emitGeneratedPattern(selection, block, node, blocks))
					throw std::logic_error(
					    std::string("no generated fallback for ") +
					    sdOpcodeName(node.opcode()));
				break;
			}
			case generated::CustomSelector::SMin:
			case generated::CustomSelector::SMax: {
				ValueType resultType = node.resultTypes().front();
				if (resultType == ValueType::V4I32) {
					if (!emitGeneratedPattern(selection, block, node, blocks))
						throw std::logic_error(
						    "no generated vector min/max pattern");
					break;
				}
				if (resultType != ValueType::I32)
					throw std::logic_error("unsupported signed min/max type");

				MachineInstr compare(Opcode::CMPWrr);
				compare.addOperand(selection.makeUse(node.operands()[0]))
				    .addOperand(selection.makeUse(node.operands()[1]))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, true, true));
				selection.emit(block, std::move(compare));

				MachineInstr select(Opcode::CSELW);
				select.addOperand(selection.makeDef(node))
				    .addOperand(selection.makeUse(node.operands()[0]))
				    .addOperand(selection.makeUse(node.operands()[1]))
				    .addOperand(MachineOperand::condition(
				        node.opcode() == SDOpcode::SMin ? CondCode::LT
				                                        : CondCode::GT))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, false, true));
				selection.emit(block, std::move(select), &node);
				break;
			}
			case generated::CustomSelector::MulMod: {
				SDNode *modulusNode = node.operands()[2].node;
				if (!modulusNode ||
				    modulusNode->opcode() != SDOpcode::Constant ||
				    modulusNode->integer == 0)
					throw std::logic_error("mulmod intrinsic requires a "
					                       "non-zero constant modulus");

				auto modulus = static_cast<std::int32_t>(modulusNode->integer);
				division::SignedDivisorInfo info =
				    division::analyzeSignedDivisor(modulus);

				VReg productReg = selection.createVReg(ValueType::Ptr);
				MachineInstr multiply(Opcode::SMULLXrr);
				multiply
				    .addOperand(
				        MachineOperand::vreg(productReg, RegClass::GPR64, true))
				    .addOperand(selection.makeUse(node.operands()[0]))
				    .addOperand(selection.makeUse(node.operands()[1]));
				MachineInstr &multiplyDef =
				    selection.emit(block, std::move(multiply));
				registerInfo.setDefinition(productReg, &multiplyDef);
				MachineOperand product =
				    MachineOperand::vreg(productReg, RegClass::GPR64);

				auto emitXTemp =
				    [&](Opcode opcode,
				        std::vector<MachineOperand> ops) -> MachineOperand {
					VReg reg = selection.createVReg(ValueType::Ptr);
					MachineInstr instruction(opcode);
					instruction.addOperand(
					    MachineOperand::vreg(reg, RegClass::GPR64, true));
					for (MachineOperand &op : ops)
						instruction.addOperand(std::move(op));
					MachineInstr &inserted =
					    selection.emit(block, std::move(instruction));
					registerInfo.setDefinition(reg, &inserted);
					return MachineOperand::vreg(reg, RegClass::GPR64);
				};

				// Fall back to a 64-bit sdiv for unsupported moduli.
				if (!info.reducible || modulus < 0) {
					VReg modulusReg = selection.createVReg(ValueType::Ptr);
					MachineInstr materialize(Opcode::MOVi64);
					materialize
					    .addOperand(MachineOperand::vreg(modulusReg,
					                                     RegClass::GPR64, true))
					    .addOperand(MachineOperand::immediate(
					        static_cast<std::uint64_t>(modulus)));
					MachineInstr &modulusDef =
					    selection.emit(block, std::move(materialize));
					registerInfo.setDefinition(modulusReg, &modulusDef);
					MachineOperand modulusOp =
					    MachineOperand::vreg(modulusReg, RegClass::GPR64);

					MachineOperand quotient =
					    emitXTemp(Opcode::SDIVXrr, {product, modulusOp});
					MachineOperand remainder = emitXTemp(
					    Opcode::MSUBXrrr, {quotient, modulusOp, product});
					MachineInstr narrow(Opcode::COPYXtoW);
					narrow.addOperand(selection.makeDef(node))
					    .addOperand(remainder);
					selection.emit(block, std::move(narrow), &node);
					break;
				}

				// Toward-zero rem: abs(product) mod |m|, then restore sign.
				MachineInstr compareProduct(Opcode::CMPXri);
				compareProduct.addOperand(product)
				    .addOperand(MachineOperand::immediate(0))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, true, true));
				selection.emit(block, std::move(compareProduct));

				MachineOperand negatedProduct =
				    emitXTemp(Opcode::NEGX, {product});
				VReg absReg = selection.createVReg(ValueType::Ptr);
				MachineInstr selectAbs(Opcode::CSELX);
				selectAbs
				    .addOperand(
				        MachineOperand::vreg(absReg, RegClass::GPR64, true))
				    .addOperand(negatedProduct)
				    .addOperand(product)
				    .addOperand(MachineOperand::condition(CondCode::LT))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, false, true));
				MachineInstr &absDef =
				    selection.emit(block, std::move(selectAbs));
				registerInfo.setDefinition(absReg, &absDef);
				MachineOperand absolute =
				    MachineOperand::vreg(absReg, RegClass::GPR64);

				MachineOperand remainderX;
				if (info.powerOfTwo) {
					VReg absW = selection.createVReg(ValueType::I32);
					MachineInstr narrowAbs(Opcode::COPYXtoW);
					narrowAbs
					    .addOperand(
					        MachineOperand::vreg(absW, RegClass::GPR32, true))
					    .addOperand(absolute);
					MachineInstr &absWDef =
					    selection.emit(block, std::move(narrowAbs));
					registerInfo.setDefinition(absW, &absWDef);

					VReg remW = selection.createVReg(ValueType::I32);
					MachineInstr andMask(Opcode::ANDWri);
					andMask
					    .addOperand(
					        MachineOperand::vreg(remW, RegClass::GPR32, true))
					    .addOperand(MachineOperand::vreg(absW, RegClass::GPR32))
					    .addOperand(MachineOperand::immediate(
					        static_cast<std::uint64_t>(info.magnitude - 1)));
					MachineInstr &remWDef =
					    selection.emit(block, std::move(andMask));
					registerInfo.setDefinition(remW, &remWDef);

					remainderX = emitXTemp(
					    Opcode::UXTW,
					    {MachineOperand::vreg(remW, RegClass::GPR32)});
				} else {
					division::BarrettModulus64 barrett =
					    division::computeBarrettModulus64(info.magnitude);

					VReg muReg = selection.createVReg(ValueType::Ptr);
					MachineInstr materializeMu(Opcode::MOVi64);
					materializeMu
					    .addOperand(
					        MachineOperand::vreg(muReg, RegClass::GPR64, true))
					    .addOperand(MachineOperand::immediate(barrett.mu));
					MachineInstr &muDef =
					    selection.emit(block, std::move(materializeMu));
					registerInfo.setDefinition(muReg, &muDef);

					VReg modulusReg = selection.createVReg(ValueType::Ptr);
					MachineInstr materializeMod(Opcode::MOVi64);
					materializeMod
					    .addOperand(MachineOperand::vreg(modulusReg,
					                                     RegClass::GPR64, true))
					    .addOperand(MachineOperand::immediate(
					        static_cast<std::uint64_t>(info.magnitude)));
					MachineInstr &modDef =
					    selection.emit(block, std::move(materializeMod));
					registerInfo.setDefinition(modulusReg, &modDef);
					MachineOperand modulusOp =
					    MachineOperand::vreg(modulusReg, RegClass::GPR64);

					MachineOperand quotient = emitXTemp(
					    Opcode::UMULHXrr,
					    {absolute,
					     MachineOperand::vreg(muReg, RegClass::GPR64)});
					MachineOperand provisional = emitXTemp(
					    Opcode::MSUBXrrr, {quotient, modulusOp, absolute});

					MachineOperand adjusted =
					    emitXTemp(Opcode::SUBXrr, {provisional, modulusOp});
					MachineInstr compareRem(Opcode::CMPXrr);
					compareRem.addOperand(provisional)
					    .addOperand(modulusOp)
					    .addOperand(MachineOperand::physReg(
					        PhysReg::NZCV, RegClass::CCR, true, true));
					selection.emit(block, std::move(compareRem));

					VReg remReg = selection.createVReg(ValueType::Ptr);
					MachineInstr selectRem(Opcode::CSELX);
					selectRem
					    .addOperand(
					        MachineOperand::vreg(remReg, RegClass::GPR64, true))
					    .addOperand(adjusted)
					    .addOperand(provisional)
					    .addOperand(MachineOperand::condition(CondCode::HS))
					    .addOperand(MachineOperand::physReg(
					        PhysReg::NZCV, RegClass::CCR, false, true));
					MachineInstr &remDef =
					    selection.emit(block, std::move(selectRem));
					registerInfo.setDefinition(remReg, &remDef);
					remainderX = MachineOperand::vreg(remReg, RegClass::GPR64);
				}

				MachineOperand negatedRem =
				    emitXTemp(Opcode::NEGX, {remainderX});
				MachineInstr compareSign(Opcode::CMPXri);
				compareSign.addOperand(product)
				    .addOperand(MachineOperand::immediate(0))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, true, true));
				selection.emit(block, std::move(compareSign));

				VReg signedRem = selection.createVReg(ValueType::Ptr);
				MachineInstr selectSign(Opcode::CSELX);
				selectSign
				    .addOperand(
				        MachineOperand::vreg(signedRem, RegClass::GPR64, true))
				    .addOperand(negatedRem)
				    .addOperand(remainderX)
				    .addOperand(MachineOperand::condition(CondCode::LT))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, false, true));
				MachineInstr &signedDef =
				    selection.emit(block, std::move(selectSign));
				registerInfo.setDefinition(signedRem, &signedDef);

				MachineInstr narrow(Opcode::COPYXtoW);
				narrow.addOperand(selection.makeDef(node))
				    .addOperand(
				        MachineOperand::vreg(signedRem, RegClass::GPR64));
				selection.emit(block, std::move(narrow), &node);
				break;
			}
			case generated::CustomSelector::SRem:
			case generated::CustomSelector::URem: {
				bool integer64 = node.resultTypes().front() == ValueType::I64;
				SDNode *rhs = node.operands()[1].node;
				if (!integer64 && node.opcode() == SDOpcode::SRem && rhs &&
				    rhs->opcode() == SDOpcode::Constant) {
					auto divisor = static_cast<std::int32_t>(rhs->integer);
					division::SignedDivisorInfo info =
					    division::analyzeSignedDivisor(divisor);
					if (info.reducible && info.magnitude == 1) {
						MachineInstr zero(Opcode::MOVi32);
						zero.addOperand(selection.makeDef(node))
						    .addOperand(MachineOperand::immediate(0));
						selection.emit(block, std::move(zero), &node);
						break;
					}
					if (info.reducible && info.powerOfTwo) {
						MachineOperand numerator =
						    selection.makeUse(node.operands()[0]);
						MachineInstr compare(Opcode::CMPWri);
						compare.addOperand(numerator)
						    .addOperand(MachineOperand::immediate(0))
						    .addOperand(MachineOperand::physReg(
						        PhysReg::NZCV, RegClass::CCR, true, true));
						selection.emit(block, std::move(compare));

						MachineOperand magnitude = numerator;
						if (info.shift != 1) {
							VReg absolute =
							    selection.createVReg(ValueType::I32);
							MachineInstr negate(Opcode::CNEGW);
							negate
							    .addOperand(MachineOperand::vreg(
							        absolute, RegClass::GPR32, true))
							    .addOperand(numerator)
							    .addOperand(
							        MachineOperand::condition(CondCode::MI))
							    .addOperand(MachineOperand::physReg(
							        PhysReg::NZCV, RegClass::CCR, false, true));
							MachineInstr &absoluteDefinition =
							    selection.emit(block, std::move(negate));
							registerInfo.setDefinition(absolute,
							                           &absoluteDefinition);
							magnitude =
							    MachineOperand::vreg(absolute, RegClass::GPR32);
						}

						VReg masked = selection.createVReg(ValueType::I32);
						MachineInstr mask(Opcode::ANDWri);
						mask.addOperand(MachineOperand::vreg(
						                    masked, RegClass::GPR32, true))
						    .addOperand(magnitude)
						    .addOperand(MachineOperand::immediate(
						        static_cast<std::uint32_t>(info.magnitude -
						                                   1)));
						MachineInstr &maskedDefinition =
						    selection.emit(block, std::move(mask));
						registerInfo.setDefinition(masked, &maskedDefinition);

						MachineInstr restoreSign(Opcode::CNEGW);
						restoreSign.addOperand(selection.makeDef(node))
						    .addOperand(
						        MachineOperand::vreg(masked, RegClass::GPR32))
						    .addOperand(MachineOperand::condition(CondCode::MI))
						    .addOperand(MachineOperand::physReg(
						        PhysReg::NZCV, RegClass::CCR, false, true));
						selection.emit(block, std::move(restoreSign), &node);
						break;
					}
				}
				ValueType quotientType =
				    integer64 ? ValueType::I64 : ValueType::I32;
				RegClass quotientClass =
				    integer64 ? RegClass::GPR64 : RegClass::GPR32;
				VReg quotient = selection.createVReg(quotientType);
				MachineOperand quotientDef =
				    MachineOperand::vreg(quotient, quotientClass, true);
				bool reduced = !integer64 && node.opcode() == SDOpcode::SRem &&
				               rhs && rhs->opcode() == SDOpcode::Constant &&
				               emitSignedConstantDivision(
				                   block, quotientDef,
				                   selection.makeUse(node.operands()[0]),
				                   static_cast<std::int32_t>(rhs->integer));
				if (!reduced) {
					MachineInstr divide(
					    node.opcode() == SDOpcode::SRem
					        ? (integer64 ? Opcode::SDIVXrr : Opcode::SDIVWrr)
					        : (integer64 ? Opcode::UDIVXrr : Opcode::UDIVWrr));
					divide.addOperand(quotientDef)
					    .addOperand(selection.makeUse(node.operands()[0]))
					    .addOperand(selection.makeUse(node.operands()[1]));
					MachineInstr &quotientDefinition =
					    selection.emit(block, std::move(divide));
					registerInfo.setDefinition(quotient, &quotientDefinition);
				}
				MachineInstr remainder(integer64 ? Opcode::MSUBXrrr
				                                 : Opcode::MSUBWrrr);
				remainder.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::vreg(quotient, quotientClass))
				    .addOperand(selection.makeUse(node.operands()[1]))
				    .addOperand(selection.makeUse(node.operands()[0]));
				selection.emit(block, std::move(remainder), &node);
				break;
			}
			case generated::CustomSelector::ICmp:
			case generated::CustomSelector::FCmp: {
				bool floating = node.opcode() == SDOpcode::FCmp;
				bool vectorCompare =
				    node.resultTypes().front() == ValueType::V4I32;
				if (vectorCompare) {
					auto temporaryCompare = [&](Opcode opcode, SDValue left,
					                            SDValue right) {
						VReg result = selection.createVReg(ValueType::V4I32);
						MachineInstr compare(opcode);
						compare
						    .addOperand(MachineOperand::vreg(
						        result, RegClass::NEON128, true))
						    .addOperand(selection.makeUse(left))
						    .addOperand(selection.makeUse(right));
						MachineInstr &definition =
						    selection.emit(block, std::move(compare));
						registerInfo.setDefinition(result, &definition);
						return result;
					};
					auto temporaryLogical = [&](Opcode opcode, VReg left,
					                            VReg right) {
						VReg result = selection.createVReg(ValueType::V4I32);
						MachineInstr logical(opcode);
						logical
						    .addOperand(MachineOperand::vreg(
						        result, RegClass::NEON128, true))
						    .addOperand(
						        MachineOperand::vreg(left, RegClass::NEON128))
						    .addOperand(
						        MachineOperand::vreg(right, RegClass::NEON128));
						MachineInstr &definition =
						    selection.emit(block, std::move(logical));
						registerInfo.setDefinition(result, &definition);
						return result;
					};
					auto temporaryNot = [&](VReg value) {
						VReg result = selection.createVReg(ValueType::V4I32);
						MachineInstr invert(Opcode::MVNv16i8);
						invert
						    .addOperand(MachineOperand::vreg(
						        result, RegClass::NEON128, true))
						    .addOperand(
						        MachineOperand::vreg(value, RegClass::NEON128));
						MachineInstr &definition =
						    selection.emit(block, std::move(invert));
						registerInfo.setDefinition(result, &definition);
						return result;
					};
					auto emitCompare = [&](Opcode opcode, SDValue left,
					                       SDValue right) {
						MachineInstr compare(opcode);
						compare.addOperand(selection.makeDef(node))
						    .addOperand(selection.makeUse(left))
						    .addOperand(selection.makeUse(right));
						selection.emit(block, std::move(compare), &node);
					};
					auto emitLogical = [&](Opcode opcode, VReg left,
					                       VReg right) {
						MachineInstr logical(opcode);
						logical.addOperand(selection.makeDef(node))
						    .addOperand(
						        MachineOperand::vreg(left, RegClass::NEON128))
						    .addOperand(
						        MachineOperand::vreg(right, RegClass::NEON128));
						selection.emit(block, std::move(logical), &node);
					};
					auto emitNot = [&](VReg value) {
						MachineInstr invert(Opcode::MVNv16i8);
						invert.addOperand(selection.makeDef(node))
						    .addOperand(
						        MachineOperand::vreg(value, RegClass::NEON128));
						selection.emit(block, std::move(invert), &node);
					};

					SDValue left = node.operands()[0];
					SDValue right = node.operands()[1];
					if (!floating) {
						auto predicate =
						    static_cast<ICmpInst::ICmpOp>(node.predicate);
						Opcode opcode = Opcode::Invalid;
						bool swap = false;
						switch (predicate) {
						case ICmpInst::ICMP_EQ:
						case ICmpInst::ICMP_NE:
							opcode = Opcode::CMEQv4i32;
							break;
						case ICmpInst::ICMP_SGT:
							opcode = Opcode::CMGTv4i32;
							break;
						case ICmpInst::ICMP_SGE:
							opcode = Opcode::CMGEv4i32;
							break;
						case ICmpInst::ICMP_SLT:
							opcode = Opcode::CMGTv4i32;
							swap = true;
							break;
						case ICmpInst::ICMP_SLE:
							opcode = Opcode::CMGEv4i32;
							swap = true;
							break;
						case ICmpInst::ICMP_UGT:
							opcode = Opcode::CMHIv4i32;
							break;
						case ICmpInst::ICMP_UGE:
							opcode = Opcode::CMHSv4i32;
							break;
						case ICmpInst::ICMP_ULT:
							opcode = Opcode::CMHIv4i32;
							swap = true;
							break;
						case ICmpInst::ICMP_ULE:
							opcode = Opcode::CMHSv4i32;
							swap = true;
							break;
						}
						if (opcode == Opcode::Invalid)
							throw std::logic_error(
							    "unsupported integer vector predicate");
						if (predicate == ICmpInst::ICMP_NE) {
							VReg equal = temporaryCompare(opcode, left, right);
							emitNot(equal);
						} else {
							emitCompare(opcode, swap ? right : left,
							            swap ? left : right);
						}
						break;
					}

					auto predicate =
					    static_cast<FCmpInst::FCmpOp>(node.predicate);
					if (predicate == FCmpInst::FCMP_FALSE ||
					    predicate == FCmpInst::FCMP_TRUE) {
						const std::uint32_t value =
						    predicate == FCmpInst::FCMP_TRUE ? ~0U : 0U;
						emitVectorConstant(block, selection.makeDef(node),
						                   {value, value, value, value}, &node);
						break;
					}
					if (predicate == FCmpInst::FCMP_OEQ ||
					    predicate == FCmpInst::FCMP_OGT ||
					    predicate == FCmpInst::FCMP_OGE ||
					    predicate == FCmpInst::FCMP_OLT ||
					    predicate == FCmpInst::FCMP_OLE) {
						bool swap = predicate == FCmpInst::FCMP_OLT ||
						            predicate == FCmpInst::FCMP_OLE;
						Opcode opcode = predicate == FCmpInst::FCMP_OEQ
						                    ? Opcode::FCMEQv4f32
						                : predicate == FCmpInst::FCMP_OGE ||
						                        predicate == FCmpInst::FCMP_OLE
						                    ? Opcode::FCMGEv4f32
						                    : Opcode::FCMGTv4f32;
						emitCompare(opcode, swap ? right : left,
						            swap ? left : right);
						break;
					}

					auto orderedMask = [&]() {
						VReg ge =
						    temporaryCompare(Opcode::FCMGEv4f32, left, right);
						VReg le =
						    temporaryCompare(Opcode::FCMGEv4f32, right, left);
						return temporaryLogical(Opcode::ORRv16i8, ge, le);
					};
					if (predicate == FCmpInst::FCMP_ONE) {
						VReg greater =
						    temporaryCompare(Opcode::FCMGTv4f32, left, right);
						VReg less =
						    temporaryCompare(Opcode::FCMGTv4f32, right, left);
						emitLogical(Opcode::ORRv16i8, greater, less);
						break;
					}
					if (predicate == FCmpInst::FCMP_UNE) {
						VReg equal =
						    temporaryCompare(Opcode::FCMEQv4f32, left, right);
						emitNot(equal);
						break;
					}
					VReg ordered = orderedMask();
					if (predicate == FCmpInst::FCMP_ORD) {
						MachineInstr copy(Opcode::COPY);
						copy.addOperand(selection.makeDef(node))
						    .addOperand(MachineOperand::vreg(
						        ordered, RegClass::NEON128));
						selection.emit(block, std::move(copy), &node);
						break;
					}
					VReg unordered = temporaryNot(ordered);
					if (predicate == FCmpInst::FCMP_UNO) {
						MachineInstr copy(Opcode::COPY);
						copy.addOperand(selection.makeDef(node))
						    .addOperand(MachineOperand::vreg(
						        unordered, RegClass::NEON128));
						selection.emit(block, std::move(copy), &node);
						break;
					}
					Opcode compareOpcode = Opcode::Invalid;
					bool swap = false;
					if (predicate == FCmpInst::FCMP_UEQ)
						compareOpcode = Opcode::FCMEQv4f32;
					else if (predicate == FCmpInst::FCMP_UGT ||
					         predicate == FCmpInst::FCMP_ULT) {
						compareOpcode = Opcode::FCMGTv4f32;
						swap = predicate == FCmpInst::FCMP_ULT;
					} else if (predicate == FCmpInst::FCMP_UGE ||
					           predicate == FCmpInst::FCMP_ULE) {
						compareOpcode = Opcode::FCMGEv4f32;
						swap = predicate == FCmpInst::FCMP_ULE;
					}
					if (compareOpcode == Opcode::Invalid)
						throw std::logic_error(
						    "unsupported floating vector predicate");
					VReg compared =
					    temporaryCompare(compareOpcode, swap ? right : left,
					                     swap ? left : right);
					emitLogical(Opcode::ORRv16i8, compared, unordered);
					break;
				}
				bool integer64 =
				    !floating && node.operands()[0].node &&
				    node.operands()[0].node->resultTypes().front() ==
				        ValueType::I64;
				auto floatingPredicate =
				    static_cast<FCmpInst::FCmpOp>(node.predicate);
				if (floating && (floatingPredicate == FCmpInst::FCMP_FALSE ||
				                 floatingPredicate == FCmpInst::FCMP_TRUE)) {
					MachineInstr constant(Opcode::MOVi32);
					constant.addOperand(selection.makeDef(node))
					    .addOperand(MachineOperand::immediate(
					        floatingPredicate == FCmpInst::FCMP_TRUE));
					selection.emit(block, std::move(constant), &node);
					break;
				}
				SDNode *rhs = node.operands()[1].node;
				bool integerImmediate =
				    !floating && rhs && rhs->opcode() == SDOpcode::Constant &&
				    InstrInfo::acceptsImmediate(integer64 ? Opcode::CMPXri
				                                          : Opcode::CMPWri,
				                                rhs->integer);
				bool floatingZero = floating && rhs &&
				                    rhs->opcode() == SDOpcode::FPConstant &&
				                    rhs->floatingBits == 0;
				MachineInstr compare(
				    integerImmediate
				        ? (integer64 ? Opcode::CMPXri : Opcode::CMPWri)
				    : floatingZero ? Opcode::FCMPZS
				    : floating     ? Opcode::FCMPSrr
				               : (integer64 ? Opcode::CMPXrr : Opcode::CMPWrr));
				compare.addOperand(selection.makeUse(node.operands()[0]));
				if (integerImmediate)
					compare.addOperand(MachineOperand::immediate(rhs->integer));
				else if (!floatingZero)
					compare.addOperand(selection.makeUse(node.operands()[1]));
				compare.addOperand(MachineOperand::physReg(
				    PhysReg::NZCV, RegClass::CCR, true, true));
				selection.emit(block, std::move(compare));
				if (floating && (floatingPredicate == FCmpInst::FCMP_ONE ||
				                 floatingPredicate == FCmpInst::FCMP_UEQ)) {
					VReg first = selection.createVReg(ValueType::I1);
					MachineInstr firstSet(Opcode::CSETW);
					firstSet
					    .addOperand(
					        MachineOperand::vreg(first, RegClass::GPR32, true))
					    .addOperand(MachineOperand::condition(
					        floatingPredicate == FCmpInst::FCMP_ONE
					            ? CondCode::NE
					            : CondCode::EQ))
					    .addOperand(MachineOperand::physReg(
					        PhysReg::NZCV, RegClass::CCR, false, true));
					MachineInstr &firstDefinition =
					    selection.emit(block, std::move(firstSet));
					registerInfo.setDefinition(first, &firstDefinition);
					VReg second = selection.createVReg(ValueType::I1);
					MachineInstr secondSet(Opcode::CSETW);
					secondSet
					    .addOperand(
					        MachineOperand::vreg(second, RegClass::GPR32, true))
					    .addOperand(MachineOperand::condition(
					        floatingPredicate == FCmpInst::FCMP_ONE
					            ? CondCode::VC
					            : CondCode::VS))
					    .addOperand(MachineOperand::physReg(
					        PhysReg::NZCV, RegClass::CCR, false, true));
					MachineInstr &secondDefinition =
					    selection.emit(block, std::move(secondSet));
					registerInfo.setDefinition(second, &secondDefinition);
					MachineInstr combine(floatingPredicate == FCmpInst::FCMP_ONE
					                         ? Opcode::ANDWrr
					                         : Opcode::ORRWrr);
					combine.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::vreg(first, RegClass::GPR32))
					    .addOperand(
					        MachineOperand::vreg(second, RegClass::GPR32));
					selection.emit(block, std::move(combine), &node);
					break;
				}
				MachineInstr set(Opcode::CSETW);
				set.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::condition(
				        floating ? generated::floatingCondition(node.predicate)
				                 : generated::integerCondition(node.predicate)))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, false, true));
				selection.emit(block, std::move(set), &node);
				break;
			}
			case generated::CustomSelector::Select: {
				RegClass selectedClass =
				    selection.getValueClass(node.operands()[1]);
				if (selectedClass == RegClass::NEON128) {
					VReg mask = 0;
					bool disposableMask = false;
					if (selection.getValueClass(node.operands()[0]) ==
					    RegClass::NEON128) {
						mask = selection.getResult(node.operands()[0]);
					} else {
						VReg scalarMask = selection.createVReg(ValueType::I32);
						MachineInstr negate(Opcode::NEGW);
						negate
						    .addOperand(MachineOperand::vreg(
						        scalarMask, RegClass::GPR32, true))
						    .addOperand(selection.makeUse(node.operands()[0]));
						MachineInstr &negateDefinition =
						    selection.emit(block, std::move(negate));
						registerInfo.setDefinition(scalarMask,
						                           &negateDefinition);

						mask = selection.createVReg(ValueType::V4I32);
						MachineInstr duplicate(Opcode::DUPv4i32);
						duplicate
						    .addOperand(MachineOperand::vreg(
						        mask, RegClass::NEON128, true))
						    .addOperand(MachineOperand::vreg(scalarMask,
						                                     RegClass::GPR32));
						MachineInstr &duplicateDefinition =
						    selection.emit(block, std::move(duplicate));
						registerInfo.setDefinition(mask, &duplicateDefinition);
						disposableMask = true;
					}

					VReg writableMask = mask;
					if (!disposableMask) {
						writableMask =
						    selection.createVReg(node.resultTypes().front());
						MachineInstr copyMask(Opcode::COPY);
						copyMask
						    .addOperand(MachineOperand::vreg(
						        writableMask, RegClass::NEON128, true))
						    .addOperand(
						        MachineOperand::vreg(mask, RegClass::NEON128));
						MachineInstr &copyDefinition =
						    selection.emit(block, std::move(copyMask));
						registerInfo.setDefinition(writableMask,
						                           &copyDefinition);
					}

					MachineInstr select(Opcode::BSLv16i8);
					select.addOperand(selection.makeDef(node))
					    .addOperand(MachineOperand::vreg(writableMask,
					                                     RegClass::NEON128))
					    .addOperand(selection.makeUse(node.operands()[1]))
					    .addOperand(selection.makeUse(node.operands()[2]));
					select.operands()[1].tiedTo = 0;
					select.operands()[1].isKill = disposableMask;
					selection.emit(block, std::move(select), &node);
					break;
				}
				MachineInstr compare(Opcode::CMPWri);
				compare.addOperand(selection.makeUse(node.operands()[0]))
				    .addOperand(MachineOperand::immediate(0))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, true, true));
				selection.emit(block, std::move(compare));
				Opcode opcode =
				    selectedClass == RegClass::FPR32   ? Opcode::FCSELS
				    : selectedClass == RegClass::GPR64 ? Opcode::CSELX
				                                       : Opcode::CSELW;
				MachineInstr select(opcode);
				select.addOperand(selection.makeDef(node))
				    .addOperand(selection.makeUse(node.operands()[1]))
				    .addOperand(selection.makeUse(node.operands()[2]))
				    .addOperand(MachineOperand::condition(CondCode::NE))
				    .addOperand(MachineOperand::physReg(
				        PhysReg::NZCV, RegClass::CCR, false, true));
				selection.emit(block, std::move(select), &node);
				break;
			}
			case generated::CustomSelector::GEP: {
				VReg current = selection.getResult(node.operands()[0]);
				for (unsigned i = 1; i < node.operands().size(); ++i) {
					SDNode *indexNode = node.operands()[i].node;
					unsigned scale = node.gepScales.at(i - 1);
					bool last = i + 1 == node.operands().size();
					VReg destination =
					    last ? results.at(&node)
					         : selection.createVReg(ValueType::Ptr);
					MachineInstr address;
					if (indexNode &&
					    indexNode->opcode() == SDOpcode::Constant) {
						std::int64_t offset = indexNode->integer * scale;
						if (offset >= 0 && offset <= 4095) {
							address.setOpcode(Opcode::ADDXri);
							address
							    .addOperand(MachineOperand::vreg(
							        destination, RegClass::GPR64, true))
							    .addOperand(MachineOperand::vreg(
							        current, RegClass::GPR64))
							    .addOperand(MachineOperand::immediate(offset));
						} else {
							VReg offsetReg =
							    selection.createVReg(ValueType::Ptr);
							MachineInstr materialize(Opcode::MOVi64);
							materialize
							    .addOperand(MachineOperand::vreg(
							        offsetReg, RegClass::GPR64, true))
							    .addOperand(MachineOperand::immediate(offset));
							MachineInstr &offsetDef =
							    selection.emit(block, std::move(materialize));
							registerInfo.setDefinition(offsetReg, &offsetDef);
							address.setOpcode(Opcode::ADDXrr);
							address
							    .addOperand(MachineOperand::vreg(
							        destination, RegClass::GPR64, true))
							    .addOperand(MachineOperand::vreg(
							        current, RegClass::GPR64))
							    .addOperand(MachineOperand::vreg(
							        offsetReg, RegClass::GPR64));
						}
					} else if (isPowerOfTwo(scale) && scale <= 8) {
						address.setOpcode(Opcode::ADDXrs);
						address
						    .addOperand(MachineOperand::vreg(
						        destination, RegClass::GPR64, true))
						    .addOperand(
						        MachineOperand::vreg(current, RegClass::GPR64))
						    .addOperand(selection.makeUse(node.operands()[i]))
						    .addOperand(
						        MachineOperand::immediate(log2Exact(scale)))
						    .addOperand(MachineOperand::immediate(1));
					} else if (isPowerOfTwo(scale)) {
						VReg extended = selection.createVReg(ValueType::Ptr);
						MachineInstr extend(Opcode::SXTW);
						extend
						    .addOperand(MachineOperand::vreg(
						        extended, RegClass::GPR64, true))
						    .addOperand(selection.makeUse(node.operands()[i]));
						MachineInstr &extendDef =
						    selection.emit(block, std::move(extend));
						registerInfo.setDefinition(extended, &extendDef);
						address.setOpcode(Opcode::ADDXrs);
						address
						    .addOperand(MachineOperand::vreg(
						        destination, RegClass::GPR64, true))
						    .addOperand(
						        MachineOperand::vreg(current, RegClass::GPR64))
						    .addOperand(
						        MachineOperand::vreg(extended, RegClass::GPR64))
						    .addOperand(
						        MachineOperand::immediate(log2Exact(scale)))
						    .addOperand(MachineOperand::immediate(2));
					} else {
						VReg scaleReg = selection.createVReg(ValueType::I32);
						MachineInstr materialize(Opcode::MOVi32);
						materialize
						    .addOperand(MachineOperand::vreg(
						        scaleReg, RegClass::GPR32, true))
						    .addOperand(MachineOperand::immediate(scale));
						MachineInstr &scaleDef =
						    selection.emit(block, std::move(materialize));
						registerInfo.setDefinition(scaleReg, &scaleDef);
						// AArch64 has a widening signed multiply-add that
						// exactly represents base + sext(index) * scale.
						// Select it here while all temporaries are virtual;
						// neither RA nor the printer may invent an address
						// scratch register later.
						address.setOpcode(Opcode::SMADDLXrrr);
						address
						    .addOperand(MachineOperand::vreg(
						        destination, RegClass::GPR64, true))
						    .addOperand(selection.makeUse(node.operands()[i]))
						    .addOperand(
						        MachineOperand::vreg(scaleReg, RegClass::GPR32))
						    .addOperand(
						        MachineOperand::vreg(current, RegClass::GPR64));
					}
					MachineInstr &addressDef = selection.emit(
					    block, std::move(address), last ? &node : nullptr);
					if (!last)
						registerInfo.setDefinition(destination, &addressDef);
					current = destination;
				}
				if (node.operands().size() == 1) {
					MachineInstr copy(Opcode::COPY);
					copy.addOperand(selection.makeDef(node))
					    .addOperand(selection.makeUse(node.operands()[0]));
					selection.emit(block, std::move(copy), &node);
				}
				break;
			}
			case generated::CustomSelector::InsertElement: {
				SDNode *index = node.operands()[2].node;
				if (!index || index->opcode() != SDOpcode::Constant) {
					VReg laneNumbers = selection.createVReg(ValueType::V4I32);
					emitVectorConstant(block,
					                   MachineOperand::vreg(laneNumbers,
					                                        RegClass::NEON128,
					                                        true),
					                   {0, 1, 2, 3});

					VReg selectedLane = selection.createVReg(ValueType::V4I32);
					MachineInstr duplicateLane(Opcode::DUPv4i32);
					duplicateLane
					    .addOperand(MachineOperand::vreg(
					        selectedLane, RegClass::NEON128, true))
					    .addOperand(selection.makeUse(node.operands()[2]));
					MachineInstr &duplicateLaneDefinition =
					    selection.emit(block, std::move(duplicateLane));
					registerInfo.setDefinition(selectedLane,
					                           &duplicateLaneDefinition);

					VReg mask = selection.createVReg(ValueType::V4I32);
					MachineInstr compare(Opcode::CMEQv4i32);
					compare
					    .addOperand(
					        MachineOperand::vreg(mask, RegClass::NEON128, true))
					    .addOperand(MachineOperand::vreg(laneNumbers,
					                                     RegClass::NEON128))
					    .addOperand(MachineOperand::vreg(selectedLane,
					                                     RegClass::NEON128));
					MachineInstr &compareDefinition =
					    selection.emit(block, std::move(compare));
					registerInfo.setDefinition(mask, &compareDefinition);

					ValueType vectorType = node.resultTypes().front();
					VReg insertedValue = selection.createVReg(vectorType);
					MachineInstr duplicateValue(vectorType == ValueType::V4F32
					                                ? Opcode::DUPv4f32
					                                : Opcode::DUPv4i32);
					duplicateValue
					    .addOperand(MachineOperand::vreg(
					        insertedValue, RegClass::NEON128, true))
					    .addOperand(selection.makeUse(node.operands()[1]));
					MachineInstr &duplicateValueDefinition =
					    selection.emit(block, std::move(duplicateValue));
					registerInfo.setDefinition(insertedValue,
					                           &duplicateValueDefinition);

					MachineInstr select(Opcode::BSLv16i8);
					select.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::vreg(mask, RegClass::NEON128))
					    .addOperand(MachineOperand::vreg(insertedValue,
					                                     RegClass::NEON128))
					    .addOperand(selection.makeUse(node.operands()[0]));
					select.operands()[1].tiedTo = 0;
					select.operands()[1].isKill = true;
					selection.emit(block, std::move(select), &node);
					break;
				}
				if (!emitGeneratedPattern(selection, block, node, blocks))
					throw std::logic_error(
					    "no generated fixed-lane insert pattern");
				break;
			}
			case generated::CustomSelector::ExtractElement: {
				SDNode *index = node.operands()[1].node;
				if (!index || index->opcode() != SDOpcode::Constant) {
					if (!dynamicExtractSlot)
						dynamicExtractSlot =
						    machineFunction->frameInfo().createStackObject(
						        16, 16, false);
					MachineInstr spill(Opcode::SPILL_STORE);
					spill.addOperand(selection.makeUse(node.operands()[0]))
					    .addOperand(
					        MachineOperand::frameIndex(*dynamicExtractSlot));
					spill.addMemoryOperand(MachineMemOperand{
					    MachineMemOperand::Access::Store, 16, 16, nullptr,
					    *dynamicExtractSlot, 0, true});
					selection.emit(block, std::move(spill));

					if (!dynamicExtractBase) {
						dynamicExtractBase =
						    selection.createVReg(ValueType::Ptr);
						MachineInstr lea(Opcode::LEA_FRAME);
						lea.addOperand(MachineOperand::vreg(*dynamicExtractBase,
						                                    RegClass::GPR64,
						                                    true))
						    .addOperand(MachineOperand::frameIndex(
						        *dynamicExtractSlot));
						MachineInstr &baseDefinition =
						    selection.emit(block, std::move(lea));
						registerInfo.setDefinition(*dynamicExtractBase,
						                           &baseDefinition);
					}

					VReg address = selection.createVReg(ValueType::Ptr);
					MachineInstr add(Opcode::ADDXrs);
					add.addOperand(
					       MachineOperand::vreg(address, RegClass::GPR64, true))
					    .addOperand(MachineOperand::vreg(*dynamicExtractBase,
					                                     RegClass::GPR64))
					    .addOperand(selection.makeUse(node.operands()[1]))
					    .addOperand(MachineOperand::immediate(2))
					    .addOperand(MachineOperand::immediate(1));
					MachineInstr &addressDefinition =
					    selection.emit(block, std::move(add));
					registerInfo.setDefinition(address, &addressDefinition);

					MachineInstr load(node.resultTypes().front() ==
					                          ValueType::F32
					                      ? Opcode::LDRSui
					                      : Opcode::LDRWui);
					load.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::vreg(address, RegClass::GPR64))
					    .addOperand(MachineOperand::immediate(0));
					load.addMemoryOperand(MachineMemOperand{
					    MachineMemOperand::Access::Load, 4, 4, nullptr,
					    *dynamicExtractSlot, std::nullopt, true});
					selection.emit(block, std::move(load), &node);
					break;
				}
				if (!emitGeneratedPattern(selection, block, node, blocks))
					throw std::logic_error(
					    "no generated fixed-lane extract pattern");
				break;
			}
			case generated::CustomSelector::ShuffleVector: {
				std::array<int, 4> mask{};
				for (unsigned lane = 0; lane < mask.size(); ++lane)
					mask[lane] = lane < node.shuffleMask.size()
					                 ? node.shuffleMask[lane]
					                 : 0;

				auto emitBinaryShuffle = [&](Opcode opcode) {
					MachineInstr shuffle(opcode);
					shuffle.addOperand(selection.makeDef(node))
					    .addOperand(selection.makeUse(node.operands()[0]))
					    .addOperand(selection.makeUse(node.operands()[1]));
					selection.emit(block, std::move(shuffle), &node);
				};
				auto emitUnaryShuffle = [&](Opcode opcode,
				                            unsigned sourceOperand) {
					MachineInstr shuffle(opcode);
					shuffle.addOperand(selection.makeDef(node))
					    .addOperand(
					        selection.makeUse(node.operands()[sourceOperand]));
					selection.emit(block, std::move(shuffle), &node);
				};
				auto emitCopy = [&](unsigned sourceOperand) {
					MachineInstr copy(Opcode::COPY);
					copy.addOperand(selection.makeDef(node))
					    .addOperand(
					        selection.makeUse(node.operands()[sourceOperand]));
					selection.emit(block, std::move(copy), &node);
				};
				auto emitDupLane = [&](unsigned sourceOperand, int lane) {
					MachineInstr duplicate(Opcode::DUPv4sLane);
					duplicate.addOperand(selection.makeDef(node))
					    .addOperand(
					        selection.makeUse(node.operands()[sourceOperand]))
					    .addOperand(MachineOperand::immediate(lane));
					selection.emit(block, std::move(duplicate), &node);
				};
				auto emitExt = [&](unsigned lowOperand, unsigned highOperand,
				                   int byteOffset) {
					MachineInstr extend(Opcode::EXTv16b);
					extend.addOperand(selection.makeDef(node))
					    .addOperand(
					        selection.makeUse(node.operands()[lowOperand]))
					    .addOperand(
					        selection.makeUse(node.operands()[highOperand]))
					    .addOperand(MachineOperand::immediate(byteOffset));
					selection.emit(block, std::move(extend), &node);
				};

				if (maskEquals(mask, {0, 1, 2, 3})) {
					emitCopy(0);
					break;
				}
				if (maskEquals(mask, {4, 5, 6, 7})) {
					emitCopy(1);
					break;
				}
				if (mask[0] == mask[1] && mask[1] == mask[2] &&
				    mask[2] == mask[3] && mask[0] >= 0 && mask[0] <= 7) {
					emitDupLane(mask[0] < 4 ? 0U : 1U, mask[0] & 3);
					break;
				}
				if (maskEquals(mask, {0, 4, 1, 5})) {
					emitBinaryShuffle(Opcode::ZIP1v4s);
					break;
				}
				if (maskEquals(mask, {2, 6, 3, 7})) {
					emitBinaryShuffle(Opcode::ZIP2v4s);
					break;
				}
				if (maskEquals(mask, {0, 2, 4, 6})) {
					emitBinaryShuffle(Opcode::UZP1v4s);
					break;
				}
				if (maskEquals(mask, {1, 3, 5, 7})) {
					emitBinaryShuffle(Opcode::UZP2v4s);
					break;
				}
				if (maskEquals(mask, {0, 4, 2, 6})) {
					emitBinaryShuffle(Opcode::TRN1v4s);
					break;
				}
				if (maskEquals(mask, {1, 5, 3, 7})) {
					emitBinaryShuffle(Opcode::TRN2v4s);
					break;
				}
				if (maskEquals(mask, {1, 0, 3, 2})) {
					emitUnaryShuffle(Opcode::REV64v4s, 0);
					break;
				}
				if (maskEquals(mask, {5, 4, 7, 6})) {
					emitUnaryShuffle(Opcode::REV64v4s, 1);
					break;
				}
				if (maskEquals(mask, {3, 2, 1, 0})) {
					// rev64 + ext #8 reverses four .s lanes.
					VReg reversed = selection.createVReg(ValueType::V4I32);
					MachineInstr reverse(Opcode::REV64v4s);
					reverse
					    .addOperand(MachineOperand::vreg(
					        reversed, RegClass::NEON128, true))
					    .addOperand(selection.makeUse(node.operands()[0]));
					MachineInstr &reverseDefinition =
					    selection.emit(block, std::move(reverse));
					registerInfo.setDefinition(reversed, &reverseDefinition);
					MachineInstr extend(Opcode::EXTv16b);
					extend.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::vreg(reversed, RegClass::NEON128))
					    .addOperand(
					        MachineOperand::vreg(reversed, RegClass::NEON128))
					    .addOperand(MachineOperand::immediate(8));
					selection.emit(block, std::move(extend), &node);
					break;
				}
				if (maskEquals(mask, {7, 6, 5, 4})) {
					VReg reversed = selection.createVReg(ValueType::V4I32);
					MachineInstr reverse(Opcode::REV64v4s);
					reverse
					    .addOperand(MachineOperand::vreg(
					        reversed, RegClass::NEON128, true))
					    .addOperand(selection.makeUse(node.operands()[1]));
					MachineInstr &reverseDefinition =
					    selection.emit(block, std::move(reverse));
					registerInfo.setDefinition(reversed, &reverseDefinition);
					MachineInstr extend(Opcode::EXTv16b);
					extend.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::vreg(reversed, RegClass::NEON128))
					    .addOperand(
					        MachineOperand::vreg(reversed, RegClass::NEON128))
					    .addOperand(MachineOperand::immediate(8));
					selection.emit(block, std::move(extend), &node);
					break;
				}

				bool matchedExt = false;
				for (int start = 1; start <= 3; ++start) {
					std::array<int, 4> expected = {start, start + 1, start + 2,
					                               start + 3};
					if (mask != expected)
						continue;
					emitExt(0, 1, start * 4);
					matchedExt = true;
					break;
				}
				if (matchedExt)
					break;

				const ValueType vectorType = node.resultTypes().front();
				auto byteIndices = [&](unsigned sourceOperand) {
					std::array<std::uint32_t, 4> words{};
					for (unsigned lane = 0; lane < mask.size(); ++lane) {
						const int selected = mask[lane];
						const bool belongs =
						    sourceOperand == 0 ? selected >= 0 && selected < 4
						                       : selected >= 4 && selected < 8;
						if (!belongs) {
							words[lane] = 0x10101010U;
							continue;
						}
						const unsigned sourceLane =
						    static_cast<unsigned>(selected) & 3U;
						const unsigned firstByte = sourceLane * 4;
						words[lane] = firstByte | ((firstByte + 1) << 8) |
						              ((firstByte + 2) << 16) |
						              ((firstByte + 3) << 24);
					}
					return words;
				};
				auto emitTableLookup =
				    [&](unsigned sourceOperand, MachineOperand destination,
				        SDNode *definition) -> MachineInstr & {
					VReg indices = selection.createVReg(ValueType::V4I32);
					emitVectorConstant(
					    block,
					    MachineOperand::vreg(indices, RegClass::NEON128, true),
					    byteIndices(sourceOperand));
					MachineInstr lookup(Opcode::TBL1v16i8);
					lookup.addOperand(destination)
					    .addOperand(
					        selection.makeUse(node.operands()[sourceOperand]))
					    .addOperand(
					        MachineOperand::vreg(indices, RegClass::NEON128));
					return selection.emit(block, std::move(lookup), definition);
				};

				const bool onlyFirst =
				    std::all_of(mask.begin(), mask.end(),
				                [](int lane) { return lane >= 0 && lane < 4; });
				const bool onlySecond =
				    std::all_of(mask.begin(), mask.end(),
				                [](int lane) { return lane >= 4 && lane < 8; });
				if (onlyFirst || onlySecond) {
					emitTableLookup(onlySecond ? 1U : 0U,
					                selection.makeDef(node), &node);
					break;
				}

				VReg first = selection.createVReg(vectorType);
				MachineInstr &firstDefinition = emitTableLookup(
				    0, MachineOperand::vreg(first, RegClass::NEON128, true),
				    nullptr);
				registerInfo.setDefinition(first, &firstDefinition);
				VReg second = selection.createVReg(vectorType);
				MachineInstr &secondDefinition = emitTableLookup(
				    1, MachineOperand::vreg(second, RegClass::NEON128, true),
				    nullptr);
				registerInfo.setDefinition(second, &secondDefinition);
				MachineInstr combine(Opcode::ORRv16i8);
				combine.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::vreg(first, RegClass::NEON128))
				    .addOperand(
				        MachineOperand::vreg(second, RegClass::NEON128));
				selection.emit(block, std::move(combine), &node);
				break;
			}
			case generated::CustomSelector::VectorReduceAdd: {
				VReg reduced = selection.createVReg(ValueType::F32);
				MachineInstr reduce(Opcode::ADDVv4i32);
				reduce
				    .addOperand(
				        MachineOperand::vreg(reduced, RegClass::FPR32, true))
				    .addOperand(selection.makeUse(node.operands()[0]));
				MachineInstr &reduceDef =
				    selection.emit(block, std::move(reduce));
				registerInfo.setDefinition(reduced, &reduceDef);
				MachineInstr move(Opcode::FMOVWS);
				move.addOperand(selection.makeDef(node))
				    .addOperand(MachineOperand::vreg(reduced, RegClass::FPR32));
				selection.emit(block, std::move(move), &node);
				break;
			}
			case generated::CustomSelector::Phi: {
				MachineInstr phi(Opcode::PHI);
				phi.addOperand(selection.makeDef(node));
				for (unsigned i = 0; i < node.operands().size(); ++i) {
					phi.addOperand(selection.makeUse(node.operands()[i]))
					    .addOperand(MachineOperand::block(
					        blocks.at(node.incomingBlocks.at(i))));
				}
				selection.emit(block, std::move(phi), &node);
				break;
			}
			case generated::CustomSelector::Call: {
				// A shared frame address is cheap within a call-free region,
				// but keeping it live across a call can force an otherwise
				// unnecessary callee-saved register or spill.
				dynamicExtractBase.reset();
				unsigned integerIndex = 0;
				unsigned floatIndex = 0;
				unsigned outgoingStackSize = 0;
				struct StackArgument {
					SDValue value;
					unsigned offset;
					unsigned size;
					unsigned alignment;
				};
				std::vector<StackArgument> stackArguments;
				for (unsigned i = 1; i < node.operands().size(); ++i) {
					RegClass regClass =
					    selection.getValueClass(node.operands()[i]);
					bool vectorBank = regClass == RegClass::FPR32 ||
					                  regClass == RegClass::NEON128;
					unsigned &index = vectorBank ? floatIndex : integerIndex;
					if (index >= 8) {
						unsigned alignment =
						    regClass == RegClass::NEON128 ? 16U : 8U;
						outgoingStackSize =
						    (outgoingStackSize + alignment - 1) / alignment *
						    alignment;
						stackArguments.push_back(
						    StackArgument{node.operands()[i], outgoingStackSize,
						                  regClass == RegClass::NEON128 ? 16U
						                  : regClass == RegClass::GPR64 ? 8U
						                                                : 4U,
						                  alignment});
						outgoingStackSize += alignment;
					}
					++index;
				}
				outgoingStackSize = (outgoingStackSize + 15) / 16 * 16;
				machineFunction->frameInfo().maxCallFrameSize =
				    std::max(machineFunction->frameInfo().maxCallFrameSize,
				             outgoingStackSize);
				if (outgoingStackSize) {
					MachineInstr down(Opcode::ADJCALLSTACKDOWN);
					down.addOperand(
					    MachineOperand::immediate(outgoingStackSize));
					selection.emit(block, std::move(down));
				}
				for (const StackArgument &argument : stackArguments) {
					RegClass regClass = selection.getValueClass(argument.value);
					unsigned width = regClass == RegClass::NEON128 ? 16U
					                 : regClass == RegClass::GPR64 ? 8U
					                                               : 4U;
					bool encodable = argument.offset % width == 0 &&
					                 argument.offset / width <= 4095;
					MachineOperand address =
					    MachineOperand::physReg(PhysReg::SP, RegClass::GPR64);
					std::int64_t memoryOffset = argument.offset;
					if (!encodable) {
						VReg offset = selection.createVReg(ValueType::Ptr);
						MachineInstr materialize(Opcode::MOVi64);
						materialize
						    .addOperand(MachineOperand::vreg(
						        offset, RegClass::GPR64, true))
						    .addOperand(
						        MachineOperand::immediate(argument.offset));
						MachineInstr &offsetDefinition =
						    selection.emit(block, std::move(materialize));
						registerInfo.setDefinition(offset, &offsetDefinition);

						VReg computedAddress =
						    selection.createVReg(ValueType::Ptr);
						MachineInstr add(Opcode::ADDXrr);
						add.addOperand(MachineOperand::vreg(computedAddress,
						                                    RegClass::GPR64,
						                                    true))
						    .addOperand(MachineOperand::physReg(
						        PhysReg::SP, RegClass::GPR64))
						    .addOperand(
						        MachineOperand::vreg(offset, RegClass::GPR64));
						MachineInstr &addressDefinition =
						    selection.emit(block, std::move(add));
						registerInfo.setDefinition(computedAddress,
						                           &addressDefinition);
						address = MachineOperand::vreg(computedAddress,
						                               RegClass::GPR64);
						memoryOffset = 0;
					}
					Opcode storeOpcode =
					    regClass == RegClass::FPR32     ? Opcode::STRSui
					    : regClass == RegClass::NEON128 ? Opcode::STRQui
					    : regClass == RegClass::GPR64   ? Opcode::STRXui
					                                    : Opcode::STRWui;
					MachineInstr store(storeOpcode);
					store.addOperand(selection.makeUse(argument.value))
					    .addOperand(address)
					    .addOperand(MachineOperand::immediate(memoryOffset));
					store.addMemoryOperand(MachineMemOperand{
					    MachineMemOperand::Access::Store, argument.size,
					    argument.alignment, node.origin, std::nullopt,
					    argument.offset, false});
					selection.emit(block, std::move(store));
				}
				integerIndex = 0;
				floatIndex = 0;
				const unsigned callCopyGroup = nextParallelCopyGroup++;
				for (unsigned i = 1; i < node.operands().size(); ++i) {
					RegClass regClass =
					    selection.getValueClass(node.operands()[i]);
					bool vectorBank = regClass == RegClass::FPR32 ||
					                  regClass == RegClass::NEON128;
					unsigned &index = vectorBank ? floatIndex : integerIndex;
					if (index < 8) {
						PhysReg destination =
						    vectorBank
						        ? RegisterInfo::vectorArgumentRegister(index)
						        : RegisterInfo::integerArgumentRegister(index);
						MachineInstr copy(Opcode::COPY);
						copy.parallelCopyGroup = callCopyGroup;
						copy.addOperand(MachineOperand::physReg(destination,
						                                        regClass, true))
						    .addOperand(selection.makeUse(node.operands()[i]));
						selection.emit(block, std::move(copy));
					}
					++index;
				}
				MachineInstr call(Opcode::CALL);
				call.addOperand(MachineOperand::external(node.symbol))
				    .addOperand(
				        MachineOperand::registerMask(callPreservedMask()));
				selection.emit(block, std::move(call));
				machineFunction->frameInfo().hasCalls = true;
				if (outgoingStackSize) {
					MachineInstr up(Opcode::ADJCALLSTACKUP);
					up.addOperand(MachineOperand::immediate(outgoingStackSize));
					selection.emit(block, std::move(up));
				}

				if (!node.resultTypes().empty() &&
				    node.resultTypes().front() != ValueType::Invalid) {
					RegClass resultClass =
					    registerInfo.get(results.at(&node)).regClass;
					PhysReg source = resultClass == RegClass::FPR32 ||
					                         resultClass == RegClass::NEON128
					                     ? PhysReg::V0
					                     : PhysReg::X0;
					MachineInstr copy(Opcode::COPY);
					copy.addOperand(selection.makeDef(node))
					    .addOperand(
					        MachineOperand::physReg(source, resultClass));
					selection.emit(block, std::move(copy), &node);
				}
				break;
			}
			case generated::CustomSelector::TailCall: {
				// Register-only sibling/general TCO.  Does not set hasCalls so
				// a function whose only calls are tail calls can stay
				// frameless.
				unsigned integerIndex = 0;
				unsigned floatIndex = 0;
				const unsigned callCopyGroup = nextParallelCopyGroup++;
				for (unsigned i = 1; i < node.operands().size(); ++i) {
					RegClass regClass =
					    selection.getValueClass(node.operands()[i]);
					bool vectorBank = regClass == RegClass::FPR32 ||
					                  regClass == RegClass::NEON128;
					unsigned &index = vectorBank ? floatIndex : integerIndex;
					if (index >= 8)
						throw std::logic_error(
						    "TailCall selected with stack-passed arguments");
					PhysReg destination =
					    vectorBank
					        ? RegisterInfo::vectorArgumentRegister(index)
					        : RegisterInfo::integerArgumentRegister(index);
					MachineInstr copy(Opcode::COPY);
					copy.parallelCopyGroup = callCopyGroup;
					copy.addOperand(MachineOperand::physReg(destination,
					                                        regClass, true))
					    .addOperand(selection.makeUse(node.operands()[i]));
					selection.emit(block, std::move(copy));
					++index;
				}
				MachineInstr call(Opcode::TAILCALL);
				call.addOperand(MachineOperand::external(node.symbol))
				    .addOperand(
				        MachineOperand::registerMask(callPreservedMask()));
				selection.emit(block, std::move(call));
				break;
			}
			case generated::CustomSelector::BranchCond: {
				MachineInstr conditional(Opcode::CBNZ);
				conditional.addOperand(selection.makeUse(node.operands()[1]))
				    .addOperand(MachineOperand::block(
				        blocks.at(node.incomingBlocks.at(0))));
				selection.emit(block, std::move(conditional));
				MachineInstr fallback(Opcode::B);
				fallback.addOperand(MachineOperand::block(
				    blocks.at(node.incomingBlocks.at(1))));
				selection.emit(block, std::move(fallback));
				break;
			}
			case generated::CustomSelector::Return: {
				if (node.operands().size() == 1) {
					if (!emitGeneratedPattern(selection, block, node, blocks))
						throw std::logic_error(
						    "no generated void return pattern");
					break;
				}
				if (node.operands().size() > 1) {
					RegClass resultClass =
					    selection.getValueClass(node.operands()[1]);
					PhysReg destination =
					    resultClass == RegClass::FPR32 ||
					            resultClass == RegClass::NEON128
					        ? PhysReg::V0
					        : PhysReg::X0;
					MachineInstr copy(Opcode::COPY);
					copy.addOperand(MachineOperand::physReg(destination,
					                                        resultClass, true))
					    .addOperand(selection.makeUse(node.operands()[1]));
					selection.emit(block, std::move(copy));
				}
				selection.emit(block, MachineInstr(Opcode::RET));
				break;
			}
			case generated::CustomSelector::Count:
				throw std::logic_error("invalid SelectionDAG opcode");
			default:
				throw std::logic_error(
				    std::string("custom selector is not implemented for ") +
				    sdOpcodeName(node.opcode()));
			}
		}
	}
	return machineFunction;
}

} // namespace backend::aarch64
