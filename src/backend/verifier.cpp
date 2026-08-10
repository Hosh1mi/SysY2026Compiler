#include "backend/verifier.hpp"
#include "backend/machine_analysis.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

bool classCompatible(RegClass regClass, PhysReg reg) {
    switch (regClass) {
    case RegClass::GPR32:
    case RegClass::GPR64:
        return RegisterInfo::isGPR(reg);
    case RegClass::FPR32:
    case RegClass::NEON128:
        return RegisterInfo::isVector(reg);
    case RegClass::CCR:
        return reg == PhysReg::NZCV;
    default:
        return false;
    }
}

unsigned scaledMemoryWidth(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDRWui: case Opcode::STRWui:
    case Opcode::LDRSui: case Opcode::STRSui:
        return 4;
    case Opcode::LDRDui: case Opcode::STRDui:
    case Opcode::LDRXui: case Opcode::STRXui:
        return 8;
    case Opcode::LDRQui: case Opcode::STRQui:
        return 16;
    default:
        return 0;
    }
}

unsigned pairMemoryWidth(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDPWi: case Opcode::STPWi:
    case Opcode::LDPSi: case Opcode::STPSi:
        return 4;
    case Opcode::LDPXi: case Opcode::STPXi:
    case Opcode::LDPDi: case Opcode::STPDi:
        return 8;
    case Opcode::LDPQi: case Opcode::STPQi:
        return 16;
    default:
        return 0;
    }
}

bool isGlobalMemoryOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDRWlo: case Opcode::STRWlo:
    case Opcode::LDRSlo: case Opcode::STRSlo:
    case Opcode::LDRQlo: case Opcode::STRQlo:
    case Opcode::LDRXlo: case Opcode::STRXlo:
        return true;
    default:
        return false;
    }
}

} // namespace

std::vector<VerificationError>
MachineVerifier::verify(const MachineFunction &function) const {
    std::vector<VerificationError> errors;
    std::unordered_set<const MachineBasicBlock *> blocks;
    for (const auto &block : function.blocks())
        blocks.insert(block.get());

    auto report = [&](const MachineBasicBlock *block, unsigned index,
                      std::string message) {
        errors.push_back(VerificationError{
            function.name(), block ? block->name() : std::string(), index,
            std::move(message)});
    };

    if (!function.entryBlock())
        report(nullptr, 0, "function has no entry block");
    if (function.hasProperty(MachineProperty::NoVRegs) &&
        !function.hasProperty(MachineProperty::Selected))
        report(nullptr, 0,
               "allocated function was not produced by instruction selection");
    if (function.hasProperty(MachineProperty::FrameFinalized) &&
        !function.hasProperty(MachineProperty::NoVRegs))
        report(nullptr, 0,
               "frame was finalized before register allocation");
    if (function.hasProperty(MachineProperty::BranchesRelaxed) &&
        !function.hasProperty(MachineProperty::FrameFinalized))
        report(nullptr, 0,
               "branches were relaxed before frame finalization");

    std::unordered_map<VReg, const MachineInstr *> definitions;
    std::unordered_set<const MachineInstr *> instructions;
    std::unordered_map<const MachineInstr *, const MachineBasicBlock *>
        instructionBlocks;
    std::unordered_map<const MachineInstr *, unsigned> instructionIndices;
    bool containsPHI = false;
    for (std::size_t blockIndex = 0;
         blockIndex < function.blocks().size(); ++blockIndex) {
        const auto &block = function.blocks()[blockIndex];
        for (const auto *successor : block->successors()) {
            if (!successor || !blocks.count(successor)) {
                report(block.get(), 0, "successor is not owned by function");
                continue;
            }
            if (std::find(successor->predecessors().begin(),
                          successor->predecessors().end(), block.get()) ==
                successor->predecessors().end())
                report(block.get(), 0, "CFG successor/predecessor mismatch");
        }
        for (const auto *predecessor : block->predecessors()) {
            if (!predecessor || !blocks.count(predecessor)) {
                report(block.get(), 0, "predecessor is not owned by function");
                continue;
            }
            if (std::find(predecessor->successors().begin(),
                          predecessor->successors().end(), block.get()) ==
                predecessor->successors().end())
                report(block.get(), 0, "CFG predecessor/successor mismatch");
        }

        bool sawNonPhi = false;
        bool sawTerminator = false;
        unsigned instructionIndex = 0;
        for (const auto &instruction : block->instructions()) {
            instructions.insert(&instruction);
            instructionBlocks[&instruction] = block.get();
            instructionIndices[&instruction] = instructionIndex;
            const InstrDesc &descriptor = InstrInfo::get(instruction.opcode());
            if (descriptor.opcode == Opcode::Invalid)
                report(block.get(), instructionIndex,
                       "instruction has invalid or undescribed opcode");
            if (function.hasProperty(MachineProperty::FrameFinalized) &&
                descriptor.pseudo)
                report(block.get(), instructionIndex,
                       "pseudo instruction remains after frame finalization");
            if (function.hasProperty(MachineProperty::FrameFinalized) &&
                instruction.parallelCopyGroup)
                report(block.get(), instructionIndex,
                       "parallel-copy marker remains in final MIR");

            if (instruction.opcode() == Opcode::PHI) {
                containsPHI = true;
                if (sawNonPhi)
                    report(block.get(), instructionIndex,
                           "PHI appears after a non-PHI instruction");
                if (instruction.operands().size() < 3 ||
                    instruction.operands().size() % 2 == 0) {
                    report(block.get(), instructionIndex,
                           "PHI has malformed incoming operands");
                } else {
                    std::unordered_set<const MachineBasicBlock *> incoming;
                    for (unsigned index = 2;
                         index < instruction.operands().size(); index += 2) {
                        const MachineOperand &incomingBlock =
                            instruction.operands()[index];
                        if (incomingBlock.kind() !=
                            MachineOperand::Kind::BasicBlock)
                            continue;
                        if (!incoming.insert(
                                incomingBlock.basicBlock()).second)
                            report(block.get(), instructionIndex,
                                   "PHI has duplicate incoming block");
                        if (std::find(block->predecessors().begin(),
                                      block->predecessors().end(),
                                      incomingBlock.basicBlock()) ==
                            block->predecessors().end())
                            report(block.get(), instructionIndex,
                                   "PHI incoming block is not a CFG "
                                   "predecessor");
                    }
                    for (const MachineBasicBlock *predecessor :
                         block->predecessors())
                        if (!incoming.count(predecessor))
                            report(block.get(), instructionIndex,
                                   "PHI is missing a CFG predecessor");
                }
            } else {
                sawNonPhi = true;
            }

            if (sawTerminator && !instruction.isTerminator())
                report(block.get(), instructionIndex,
                       "instruction appears after terminator");
            sawTerminator |= instruction.isTerminator();

            if (descriptor.explicitOperands != 0 &&
                instruction.operands().size() < descriptor.explicitOperands)
                report(block.get(), instructionIndex,
                       "instruction has too few operands");

            unsigned operandIndex = 0;
            for (const auto &operand : instruction.operands()) {
                if (operand.isVirtualRegister()) {
                    VReg reg = operand.virtualRegister();
                    if (!reg || !function.registerInfo().contains(reg)) {
                        report(block.get(), instructionIndex,
                               "operand references unknown virtual register");
                    } else {
                        const VRegInfo &info = function.registerInfo().get(reg);
                        if (info.regClass != operand.regClass())
                            report(block.get(), instructionIndex,
                                   "virtual register class mismatch");
                        if (operand.isDef) {
                            auto [it, inserted] =
                                definitions.emplace(reg, &instruction);
                            if (!inserted &&
                                function.hasProperty(MachineProperty::IsSSA))
                                report(block.get(), instructionIndex,
                                       "virtual register has multiple definitions");
                        }
                    }
                } else if (operand.isPhysicalRegister()) {
                    if (!classCompatible(operand.regClass(),
                                         operand.physicalRegister()))
                        report(block.get(), instructionIndex,
                               "physical register class mismatch");
                } else if (operand.kind() ==
                           MachineOperand::Kind::BasicBlock) {
                    if (!operand.basicBlock() ||
                        !blocks.count(operand.basicBlock()))
                        report(block.get(), instructionIndex,
                               "block operand is not owned by function");
                } else if (operand.kind() ==
                           MachineOperand::Kind::FrameIndex) {
                    int frameIndex = operand.frameIndex();
                    if (frameIndex < 0 ||
                        static_cast<std::size_t>(frameIndex) >=
                            function.frameInfo().objects().size())
                        report(block.get(), instructionIndex,
                               "frame-index operand is out of range");
                    if (function.hasProperty(MachineProperty::FrameFinalized))
                        report(block.get(), instructionIndex,
                               "frame index remains after frame finalization");
                }

                if (operand.tiedTo >= 0) {
                    if (static_cast<std::size_t>(operand.tiedTo) >=
                        instruction.operands().size())
                        report(block.get(), instructionIndex,
                               "tied operand index is out of range");
                    else if (!instruction.operands()[operand.tiedTo].isDef)
                        report(block.get(), instructionIndex,
                               "operand is tied to a non-def operand");
                }
                ++operandIndex;
            }

            if (instruction.mayLoad() && instruction.memoryOperands().empty())
                report(block.get(), instructionIndex,
                       "load has no machine memory operand");
            if (instruction.mayStore() && instruction.memoryOperands().empty())
                report(block.get(), instructionIndex,
                       "store has no machine memory operand");
            if (isGlobalMemoryOpcode(instruction.opcode()) &&
                (instruction.operands().size() != 3 ||
                 !instruction.operands()[1].isRegister() ||
                 instruction.operands()[2].kind() !=
                     MachineOperand::Kind::GlobalSymbol))
                report(block.get(), instructionIndex,
                       "malformed global memory addressing mode");

            if (function.hasProperty(MachineProperty::FrameFinalized)) {
                unsigned width =
                    scaledMemoryWidth(instruction.opcode());
                if (width && instruction.operands().size() >= 3) {
                    const MachineOperand &offset =
                        instruction.operands()[2];
                    if (offset.kind() !=
                            MachineOperand::Kind::Immediate ||
                        offset.immediate() < 0 ||
                        offset.immediate() % width != 0 ||
                        offset.immediate() / width > 4095)
                        report(block.get(), instructionIndex,
                               "scaled memory offset is not encodable");
                }
                unsigned pairWidth =
                    pairMemoryWidth(instruction.opcode());
                if (pairWidth &&
                    instruction.operands().size() >= 4) {
                    const MachineOperand &offset =
                        instruction.operands()[3];
                    if (offset.kind() !=
                            MachineOperand::Kind::Immediate ||
                        offset.immediate() % pairWidth != 0 ||
                        offset.immediate() / pairWidth < -64 ||
                        offset.immediate() / pairWidth > 63)
                        report(block.get(), instructionIndex,
                               "pair memory offset is not encodable");
                }
            }

            ++instructionIndex;
        }

        if (!block->successors().empty()) {
            bool hasTerminator = !block->instructions().empty() &&
                                 block->instructions().back().isTerminator();
            const MachineBasicBlock *layoutSuccessor =
                blockIndex + 1 < function.blocks().size()
                    ? function.blocks()[blockIndex + 1].get()
                    : nullptr;
            bool hasFallthrough =
                layoutSuccessor &&
                std::find(block->successors().begin(),
                          block->successors().end(),
                          layoutSuccessor) !=
                    block->successors().end();
            if (!hasTerminator && !hasFallthrough)
                report(block.get(), instructionIndex,
                       "block with successors has no terminator");
        }
    }

    if (containsPHI != function.hasProperty(MachineProperty::HasPHIs))
        report(nullptr, 0,
               containsPHI
                   ? "MIR contains PHIs without the HasPHIs property"
                   : "HasPHIs property is set but MIR contains no PHIs");

    if (function.hasProperty(MachineProperty::IsSSA)) {
        for (const auto &[reg, info] :
             function.registerInfo().virtualRegisters()) {
            auto definition = definitions.find(reg);
            if (definition == definitions.end())
                report(nullptr, 0, "SSA virtual register has no definition");
            if (info.definition && definition != definitions.end() &&
                info.definition != definition->second)
                report(nullptr, 0,
                       "MachineRegisterInfo definition does not match MIR");
            if (info.definition && !instructions.count(info.definition))
                report(nullptr, 0,
                       "MachineRegisterInfo definition for vreg %" +
                           std::to_string(reg) + " is not in MIR");
        }

        MachineDominatorTree dominators;
        dominators.analyze(function);
        for (const auto &block : function.blocks()) {
            unsigned useInstructionIndex = 0;
            for (const MachineInstr &instruction : block->instructions()) {
                for (unsigned operandIndex = 0;
                     operandIndex < instruction.operands().size();
                     ++operandIndex) {
                    const MachineOperand &operand =
                        instruction.operands()[operandIndex];
                    if (!operand.isVirtualRegister() || operand.isDef)
                        continue;
                    auto definition =
                        definitions.find(operand.virtualRegister());
                    if (definition == definitions.end())
                        continue;
                    const MachineInstr *definitionInstruction =
                        definition->second;
                    const MachineBasicBlock *definitionBlock =
                        instructionBlocks.at(definitionInstruction);

                    if (instruction.opcode() == Opcode::PHI) {
                        if (operandIndex + 1 >=
                                instruction.operands().size() ||
                            instruction.operands()[operandIndex + 1].kind() !=
                                MachineOperand::Kind::BasicBlock)
                            continue;
                        const MachineBasicBlock *incomingBlock =
                            instruction.operands()[operandIndex + 1]
                                .basicBlock();
                        if (!dominators.dominates(definitionBlock,
                                                  incomingBlock))
                            report(block.get(), useInstructionIndex,
                                   "SSA definition does not dominate PHI "
                                   "incoming edge");
                        continue;
                    }

                    if (!dominators.dominates(definitionBlock,
                                              block.get())) {
                        report(block.get(), useInstructionIndex,
                               "SSA definition does not dominate use");
                    } else if (definitionBlock == block.get() &&
                               instructionIndices.at(definitionInstruction) >=
                                   useInstructionIndex) {
                        report(block.get(), useInstructionIndex,
                               "SSA virtual register is used before its "
                               "definition");
                    }
                }
                ++useInstructionIndex;
            }
        }
    }

    if (function.hasProperty(MachineProperty::NoVRegs)) {
        for (const auto &block : function.blocks())
            for (const auto &instruction : block->instructions())
                for (const auto &operand : instruction.operands())
                    if (operand.isVirtualRegister())
                        report(block.get(), 0,
                               "virtual register remains in NoVRegs function");
    }

    if (function.hasProperty(MachineProperty::BranchesRelaxed)) {
        std::unordered_map<const MachineBasicBlock *, std::int64_t>
            blockOffsets;
        std::unordered_map<const MachineInstr *, std::int64_t>
            instructionOffsets;
        std::int64_t offset = 0;
        for (const auto &block : function.blocks()) {
            blockOffsets[block.get()] = offset;
            for (const MachineInstr &instruction : block->instructions()) {
                instructionOffsets[&instruction] = offset;
                bool elidedCopy =
                    instruction.opcode() == Opcode::COPYXtoW &&
                    instruction.operands().size() >= 2 &&
                    instruction.operands()[0].isPhysicalRegister() &&
                    instruction.operands()[1].isPhysicalRegister() &&
                    RegisterInfo::aliases(
                        instruction.operands()[0].physicalRegister(),
                        instruction.operands()[1].physicalRegister());
                if (!elidedCopy)
                    offset += 4;
            }
        }

        for (const auto &block : function.blocks()) {
            unsigned instructionIndex = 0;
            for (const MachineInstr &instruction : block->instructions()) {
                unsigned targetIndex = 0;
                std::int64_t range = 0;
                switch (instruction.opcode()) {
                case Opcode::Bcc:
                case Opcode::CBZ:
                case Opcode::CBNZ:
                    targetIndex = 1;
                    range = std::int64_t{1} << 20;
                    break;
                case Opcode::TBZ:
                case Opcode::TBNZ:
                    targetIndex = 2;
                    range = std::int64_t{1} << 15;
                    break;
                default:
                    ++instructionIndex;
                    continue;
                }
                if (instruction.operands().size() <= targetIndex ||
                    instruction.operands()[targetIndex].kind() !=
                        MachineOperand::Kind::BasicBlock) {
                    ++instructionIndex;
                    continue;
                }
                const MachineBasicBlock *target =
                    instruction.operands()[targetIndex].basicBlock();
                const std::int64_t displacement =
                    blockOffsets.at(target) -
                    instructionOffsets.at(&instruction);
                if (displacement < -range || displacement > range - 4 ||
                    displacement % 4 != 0)
                    report(block.get(), instructionIndex,
                           "conditional branch remains out of range");
                ++instructionIndex;
            }
        }
    }

    return errors;
}

void MachineVerifier::verifyOrThrow(const MachineFunction &function,
                                    const std::string &stage) const {
    auto errors = verify(function);
    if (errors.empty())
        return;

    std::ostringstream message;
    message << "MachineVerifier failed after " << stage << ":\n";
    for (const auto &error : errors) {
        message << "  " << error.function;
        if (!error.block.empty())
            message << ':' << error.block << ':' << error.instructionIndex;
        message << ": " << error.message << '\n';
    }
    throw std::logic_error(message.str());
}

} // namespace backend::aarch64
