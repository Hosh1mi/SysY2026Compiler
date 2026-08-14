// Post-register-allocation optimizations operate on physical registers and
// lower remaining machine pseudos before final scheduling.
#include "backend/post_ra_optimizations.hpp"
#include "backend/machine_analysis.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

bool PostRACopyPropagation::run(MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;

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
            PhysReg temporary =
                producer->operands()[0].physicalRegister();
            PhysReg input =
                producer->operands()[1].physicalRegister();
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
                        copy->operands()[0].physicalRegister(),
                        input) &&
                    RegisterInfo::aliases(
                        copy->operands()[1].physicalRegister(),
                        temporary)) {
                    producer->operands()[0] =
                        MachineOperand::physReg(
                            input,
                            producer->operands()[0].regClass(),
                            true);
                    instructions.erase(copy);
                    changed = true;
                    break;
                }

                bool conflicts = false;
                for (const MachineOperand &operand :
                     copy->operands())
                    if (operand.isPhysicalRegister() &&
                        (RegisterInfo::aliases(
                             operand.physicalRegister(),
                             temporary) ||
                         RegisterInfo::aliases(
                             operand.physicalRegister(),
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

    MachinePhysicalRegisterLiveness physicalLiveness(function);

    // Forward a physical copy through its local uses until the copied value
    // is provably killed.  This handles call results such as
    // `COPY s16, s0` without treating calls as transparent: argument
    // registers are implicit call uses, caller-saved temporaries are call
    // clobbers, and branch edges remain conservatively live-out.
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto copy = instructions.begin();
             copy != instructions.end();) {
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
            PhysReg destination =
                copy->operands()[0].physicalRegister();
            PhysReg source =
                copy->operands()[1].physicalRegister();
            if (RegisterInfo::aliases(destination, source)) {
                ++copy;
                continue;
            }

            bool sourceAvailable = true;
            bool killed = false;
            bool blocked = false;
            std::vector<MachineOperand *> rewrites;
            for (auto scan = std::next(copy);
                 scan != instructions.end(); ++scan) {
                if (scan->isCall()) {
                    if (RegisterInfo::isArgumentRegister(destination)) {
                        blocked = true;
                    } else if (
                        RegisterInfo::isCallerSaved(destination)) {
                        killed = true;
                    }
                    break;
                }
                if (scan->isTerminator()) {
                    bool terminatorUsesDestination = false;
                    for (const MachineOperand &operand :
                         scan->operands())
                        if (operand.isPhysicalRegister() &&
                            !operand.isDef &&
                            RegisterInfo::aliases(
                                operand.physicalRegister(),
                                destination)) {
                            terminatorUsesDestination = true;
                            break;
                        }
                    // A value consumed by the terminator is not necessarily
                    // live-out: conditional branches use it on the current
                    // block's outgoing edge.  Keep the copy in that case.
                    if (terminatorUsesDestination) {
                        blocked = true;
                    } else if (!physicalLiveness.liveOut(block.get()).count(
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
                        definesDestination |= RegisterInfo::aliases(
                            reg, destination);
                        definesSource |= RegisterInfo::aliases(
                            reg, source);
                        continue;
                    }
                    if (!RegisterInfo::aliases(reg, destination))
                        continue;
                    if (!sourceAvailable || operand.tiedTo >= 0 ||
                        operand.regClass() !=
                            copy->operands()[1].regClass()) {
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
                    source, copy->operands()[1].regClass(),
                    false, implicit);
            }
            copy = instructions.erase(copy);
            changed = true;
        }
    }

    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto copy = instructions.begin();
             copy != instructions.end();) {
            if (copy->opcode() != Opcode::COPY ||
                copy->operands().size() != 2 ||
                !copy->operands()[0].isPhysicalRegister() ||
                !copy->operands()[1].isPhysicalRegister()) {
                ++copy;
                continue;
            }
            PhysReg destination =
                copy->operands()[0].physicalRegister();
            PhysReg source = copy->operands()[1].physicalRegister();
            RegClass destinationClass =
                copy->operands()[0].regClass();
            RegClass sourceClass = copy->operands()[1].regClass();
            if (destinationClass != sourceClass) {
                ++copy;
                continue;
            }

            bool reachedRedefinition = false;
            bool blocked = false;
            std::vector<MachineOperand *> rewrites;
            for (auto scan = std::next(copy);
                 scan != instructions.end(); ++scan) {
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
                        definesSource |=
                            RegisterInfo::aliases(reg, source);
                    } else if (RegisterInfo::aliases(
                                   reg, destination)) {
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
                *operand = MachineOperand::physReg(
                    source, sourceClass, isDef, implicit);
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

bool A53FPRegisterBalancing::run(MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;

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

bool PostRARedundantCopyElimination::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;

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

bool PostRAInstructionExpansion::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;
    bool changed = false;
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto insert = instructions.begin();
             insert != instructions.end(); ++insert) {
            const bool vectorInsert =
                insert->opcode() == Opcode::INSv4i32 ||
                insert->opcode() == Opcode::INSv4f32;
            const bool vectorAccumulate =
                insert->opcode() == Opcode::MLAv4i32 ||
                insert->opcode() == Opcode::MLSv4i32 ||
                insert->opcode() == Opcode::FMLAv4f32 ||
                insert->opcode() == Opcode::FMLSv4f32;
            const bool vectorSelect =
                insert->opcode() == Opcode::BSLv16i8;
            if (!vectorInsert && !vectorAccumulate && !vectorSelect)
                continue;
            if (insert->operands().size() != 4 ||
                !insert->operands()[0].isPhysicalRegister() ||
                !insert->operands()[0].isDef ||
                !insert->operands()[1].isPhysicalRegister())
                throw std::logic_error(
                    "malformed tied vector instruction after allocation");
            MachineOperand &destination = insert->operands()[0];
            MachineOperand &source = insert->operands()[1];
            if (!destination.isSameRegisterAs(source)) {
                MachineInstr copy(Opcode::COPY);
                copy.addOperand(MachineOperand::physReg(
                                    destination.physicalRegister(),
                                    RegClass::NEON128, true))
                    .addOperand(MachineOperand::physReg(
                        source.physicalRegister(),
                        RegClass::NEON128));
                instructions.insert(insert, std::move(copy));
            }
            MachineOperand tiedUse = MachineOperand::physReg(
                destination.physicalRegister(), RegClass::NEON128);
            tiedUse.tiedTo = 0;
            source = std::move(tiedUse);
            changed = true;
        }
    }
    return changed;
}

namespace {

bool isAArch64LogicalImmediate(std::uint64_t value, unsigned width) {
    const std::uint64_t widthMask =
        width == 64 ? ~std::uint64_t{0}
                    : (std::uint64_t{1} << width) - 1;
    value &= widthMask;
    if (value == 0 || value == widthMask)
        return false;

    for (unsigned elementWidth = 2; elementWidth <= width;
         elementWidth *= 2) {
        const std::uint64_t elementMask =
            elementWidth == 64
                ? ~std::uint64_t{0}
                : (std::uint64_t{1} << elementWidth) - 1;
        const std::uint64_t element = value & elementMask;
        bool repeats = true;
        for (unsigned offset = elementWidth; offset < width;
             offset += elementWidth)
            repeats &= ((value >> offset) & elementMask) == element;
        if (!repeats || element == 0 || element == elementMask)
            continue;

        for (unsigned ones = 1; ones < elementWidth; ++ones) {
            const std::uint64_t run =
                (std::uint64_t{1} << ones) - 1;
            for (unsigned rotation = 0; rotation < elementWidth;
                 ++rotation) {
                const std::uint64_t rotated =
                    rotation == 0
                        ? run
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
                            bool enableMovn,
                            bool enableLogicalImmediate) {
    if (materialize->operands().size() != 2 ||
        !materialize->operands()[0].isPhysicalRegister() ||
        !materialize->operands()[0].isDef ||
        materialize->operands()[1].kind() !=
            MachineOperand::Kind::Immediate)
        throw std::logic_error(
            "malformed integer materialization after register allocation");

    const MachineOperand &destination = materialize->operands()[0];
    const PhysReg reg = destination.physicalRegister();
    const RegClass regClass = destination.regClass();
    const bool wide = materialize->opcode() == Opcode::MOVi64;
    if ((wide && regClass != RegClass::GPR64) ||
        (!wide && regClass != RegClass::GPR32))
        throw std::logic_error(
            "integer materialization register class mismatch");

    const std::uint64_t value = wide
        ? static_cast<std::uint64_t>(materialize->operands()[1].immediate())
        : static_cast<std::uint32_t>(materialize->operands()[1].immediate());
    const unsigned pieces = wide ? 4U : 2U;

    auto piece = [&](unsigned index) {
        return (value >> (index * 16)) & 0xffffU;
    };
    unsigned nonzeroPieces = 0;
    unsigned nonOnesPieces = 0;
    for (unsigned index = 0; index < pieces; ++index) {
        nonzeroPieces += piece(index) != 0;
        nonOnesPieces += piece(index) != 0xffffU;
    }
    const unsigned movzCost = std::max(1U, nonzeroPieces);
    const unsigned movnCost = std::max(1U, nonOnesPieces);
    const bool useMovn = enableMovn && movnCost < movzCost;
    const unsigned moveWideCost = useMovn ? movnCost : movzCost;

    if (enableLogicalImmediate && moveWideCost > 1 &&
        isAArch64LogicalImmediate(value, wide ? 64U : 32U)) {
        MachineInstr logicalImmediate(wide ? Opcode::ORRXri
                                           : Opcode::ORRWri);
        logicalImmediate
            .addOperand(MachineOperand::physReg(reg, regClass, true))
            .addOperand(MachineOperand::physReg(PhysReg::XZR, regClass))
            .addOperand(MachineOperand::immediate(
                static_cast<std::int64_t>(value)));
        instructions.insert(materialize, std::move(logicalImmediate));
        instructions.erase(materialize);
        return;
    }

    unsigned first = 0;
    for (unsigned index = 0; index < pieces; ++index) {
        const bool needsSeed = useMovn ? piece(index) != 0xffffU
                                      : piece(index) != 0;
        if (needsSeed) {
            first = index;
            break;
        }
    }

    auto emitMovz = [&](unsigned slice, std::uint64_t imm) {
        MachineInstr movz(Opcode::MOVZ);
        movz.addOperand(MachineOperand::physReg(reg, regClass, true));
        movz.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(imm)));
        movz.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(slice * 16)));
        instructions.insert(materialize, std::move(movz));
    };
    auto emitMovn = [&](unsigned slice, std::uint64_t imm) {
        MachineInstr movn(Opcode::MOVN);
        movn.addOperand(MachineOperand::physReg(reg, regClass, true));
        movn.addOperand(
            MachineOperand::immediate(static_cast<std::int64_t>(imm)));
        movn.addOperand(MachineOperand::immediate(
            static_cast<std::int64_t>(slice * 16)));
        instructions.insert(materialize, std::move(movn));
    };
    auto emitMovk = [&](unsigned slice, std::uint64_t imm) {
        MachineInstr movk(Opcode::MOVK);
        movk.addOperand(MachineOperand::physReg(reg, regClass, true));
        MachineOperand use = MachineOperand::physReg(reg, regClass);
        use.tiedTo = 0;
        use.isKill = true;
        movk.addOperand(std::move(use));
        movk.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(imm)));
        movk.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(slice * 16)));
        instructions.insert(materialize, std::move(movk));
    };

    if (useMovn)
        emitMovn(first, (~piece(first)) & 0xffffU);
    else
        emitMovz(first, piece(first));
    for (unsigned index = 0; index < pieces; ++index) {
        if (index == first)
            continue;
        const std::uint64_t current = piece(index);
        if ((useMovn && current == 0xffffU) || (!useMovn && current == 0))
            continue;
        emitMovk(index, current);
    }
    instructions.erase(materialize);
}

} // namespace

bool PostRAInstructionExpansion::expandConstantMaterializations(
    MachineFunction &function, bool enableMovn,
    bool enableLogicalImmediate) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;
    bool changed = false;
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            auto current = it++;
            if (current->opcode() != Opcode::MOVi32 &&
                current->opcode() != Opcode::MOVi64)
                continue;
            expandIntegerImmediate(instructions, current, enableMovn,
                                   enableLogicalImmediate);
            changed = true;
        }
    }
    return changed;
}

} // namespace backend::aarch64
