#include "../../include/backend/arm64/verifier.hpp"

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

    std::unordered_map<VReg, const MachineInstr *> definitions;
    std::unordered_set<const MachineInstr *> instructions;
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
                if (sawNonPhi)
                    report(block.get(), instructionIndex,
                           "PHI appears after a non-PHI instruction");
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
    }

    if (function.hasProperty(MachineProperty::NoVRegs)) {
        for (const auto &block : function.blocks())
            for (const auto &instruction : block->instructions())
                for (const auto &operand : instruction.operands())
                    if (operand.isVirtualRegister())
                        report(block.get(), 0,
                               "virtual register remains in NoVRegs function");
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
