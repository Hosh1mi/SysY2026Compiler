// This file folds address calculations before allocation and selects physical
// pre/post-indexed addressing forms after frame finalization.
#include "backend/post_ra_optimizations.hpp"
#include "backend/pre_ra_optimizations.hpp"
#include "backend/machine_analysis.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

unsigned scaledImmediateWidth(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDRWui: case Opcode::STRWui:
    case Opcode::LDRSui: case Opcode::STRSui:
        return 4;
    case Opcode::LDRXui: case Opcode::STRXui:
        return 8;
    case Opcode::LDRQui: case Opcode::STRQui:
        return 16;
    default:
        return 0;
    }
}

Opcode registerOffsetOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDRWui: return Opcode::LDRWro;
    case Opcode::STRWui: return Opcode::STRWro;
    case Opcode::LDRSui: return Opcode::LDRSro;
    case Opcode::STRSui: return Opcode::STRSro;
    case Opcode::LDRXui: return Opcode::LDRXro;
    case Opcode::STRXui: return Opcode::STRXro;
    case Opcode::LDRQui: return Opcode::LDRQro;
    case Opcode::STRQui: return Opcode::STRQro;
    default: return Opcode::Invalid;
    }
}

bool scaledImmediateEncodable(std::int64_t offset, unsigned width) {
    return width != 0 && offset >= 0 && offset % width == 0 &&
           static_cast<std::uint64_t>(offset / width) <= 4095;
}

unsigned registerOffsetShift(unsigned width) {
    unsigned shift = 0;
    while ((1U << shift) < width)
        ++shift;
    return shift;
}

bool isScaledImmediateMemory(Opcode opcode) {
    return scaledImmediateWidth(opcode) != 0;
}

} // namespace

bool PreRAAddressingFolder::run(MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::IsSSA) ||
        !function.hasProperty(MachineProperty::Selected) ||
        function.hasProperty(MachineProperty::NoVRegs))
        return false;

    enum class AddressKind : std::uint8_t {
        Invalid,
        Immediate,
        Index,
    };
    struct Address {
        AddressKind kind = AddressKind::Invalid;
        VReg base = 0;
        MachineOperand index;
        std::int64_t offset = 0;
        std::int64_t shift = 0;
        std::int64_t extension = 0;
    };

    MachineRegisterIndex registers(function);

    std::unordered_map<VReg, Address> memo;
    std::unordered_set<VReg> resolving;
    auto resolve = [&](auto &&self, VReg reg) -> Address {
        auto cached = memo.find(reg);
        if (cached != memo.end())
            return cached->second;

        Address form;
        form.kind = AddressKind::Immediate;
        form.base = reg;
        if (!function.registerInfo().contains(reg) ||
            !resolving.insert(reg).second)
            return Address{};

        MachineInstr *definition = registers.uniqueDefinition(reg);
        if (definition && !definition->operands().empty() &&
            definition->operands()[0].isVirtualRegister() &&
            definition->operands()[0].isDef &&
            definition->operands()[0].virtualRegister() == reg &&
            definition->operands()[0].regClass() == RegClass::GPR64) {
            const auto &operands = definition->operands();
            if (definition->opcode() == Opcode::COPY &&
                operands.size() == 2 &&
                operands[1].isVirtualRegister() &&
                operands[1].regClass() == RegClass::GPR64) {
                form = self(self, operands[1].virtualRegister());
                // A register-offset access carries both the base and index
                // live until the memory instruction.  Folding a shared
                // address trades one reusable address value for two longer
                // live ranges and can introduce spills.  Require every COPY
                // on the path to have a single consumer so the folded access
                // replaces, rather than duplicates, the address computation.
                if (form.kind == AddressKind::Index &&
                    registers.useCount(reg) != 1) {
                    form = Address{};
                    form.kind = AddressKind::Immediate;
                    form.base = reg;
                }
            } else if (definition->opcode() == Opcode::ADDXri &&
                       operands.size() == 3 &&
                       operands[1].isVirtualRegister() &&
                       operands[1].regClass() == RegClass::GPR64 &&
                       operands[2].kind() ==
                           MachineOperand::Kind::Immediate) {
                Address base =
                    self(self, operands[1].virtualRegister());
                std::int64_t combined = 0;
                if (base.kind == AddressKind::Immediate &&
                    !__builtin_add_overflow(
                        base.offset, operands[2].immediate(), &combined)) {
                    form = std::move(base);
                    form.offset = combined;
                }
            } else if (definition->opcode() == Opcode::ADDXrs &&
                       operands.size() == 5 &&
                       operands[1].isVirtualRegister() &&
                       operands[1].regClass() == RegClass::GPR64 &&
                       operands[2].isVirtualRegister() &&
                       operands[3].kind() ==
                           MachineOperand::Kind::Immediate &&
                       operands[4].kind() ==
                           MachineOperand::Kind::Immediate) {
                Address base =
                    self(self, operands[1].virtualRegister());
                std::int64_t extension = operands[4].immediate();
                bool validIndex =
                    (extension == 0 || extension == 1)
                        ? operands[2].regClass() == RegClass::GPR32
                        : extension == 2 &&
                              operands[2].regClass() == RegClass::GPR64;
                if (base.kind == AddressKind::Immediate &&
                    base.offset == 0 && validIndex &&
                    registers.useCount(reg) == 1) {
                    form.kind = AddressKind::Index;
                    form.base = base.base;
                    form.index = operands[2];
                    form.index.isDef = false;
                    form.shift = operands[3].immediate();
                    form.extension = extension;
                }
            }
        }

        resolving.erase(reg);
        memo.emplace(reg, form);
        return form;
    };

    bool changed = false;
    for (auto &block : function.blocks()) {
        for (MachineInstr &instruction : block->instructions()) {
            if (!isScaledImmediateMemory(instruction.opcode()) ||
                instruction.operands().size() != 3 ||
                !instruction.operands()[1].isVirtualRegister() ||
                instruction.operands()[2].kind() !=
                    MachineOperand::Kind::Immediate)
                continue;

            VReg address = instruction.operands()[1].virtualRegister();
            std::int64_t memOffset =
                instruction.operands()[2].immediate();
            unsigned width = scaledImmediateWidth(instruction.opcode());
            Address form = resolve(resolve, address);

            if (form.kind == AddressKind::Immediate &&
                (form.base != address || form.offset != 0)) {
                std::int64_t folded = 0;
                if (__builtin_add_overflow(form.offset, memOffset,
                                           &folded) ||
                    !scaledImmediateEncodable(folded, width))
                    continue;
                instruction.operands()[1] = MachineOperand::vreg(
                    form.base, RegClass::GPR64);
                instruction.operands()[2] =
                    MachineOperand::immediate(folded);
                changed = true;
                continue;
            }

            if (form.kind != AddressKind::Index || memOffset != 0)
                continue;
            std::int64_t legalShift = static_cast<std::int64_t>(
                registerOffsetShift(width));
            if (form.shift != 0 && form.shift != legalShift)
                continue;
            Opcode ro = registerOffsetOpcode(instruction.opcode());
            if (ro == Opcode::Invalid)
                continue;
            MachineOperand value = instruction.operands()[0];
            instruction.setOpcode(ro);
            instruction.operands().clear();
            instruction.addOperand(std::move(value))
                .addOperand(MachineOperand::vreg(
                    form.base, RegClass::GPR64))
                .addOperand(std::move(form.index))
                .addOperand(MachineOperand::immediate(form.shift))
                .addOperand(MachineOperand::immediate(form.extension));
            changed = true;
        }
    }
    return changed;
}

bool PostRAAddressingOptimizer::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;

    bool changed = false;

    // Spill code is inserted after the pre-RA load/store optimizer, and its
    // frame offsets are not known until frame lowering.  Pair adjacent scalar
    // and vector stack accesses once physical registers and addresses are
    // available.
    struct StackPairKind {
        bool load = false;
        Opcode pairOpcode = Opcode::Invalid;
        std::int64_t stride = 0;
    };
    auto stackPairKind = [](Opcode opcode) -> std::optional<StackPairKind> {
        switch (opcode) {
        case Opcode::LDRWui: return StackPairKind{true, Opcode::LDPWi, 4};
        case Opcode::STRWui: return StackPairKind{false, Opcode::STPWi, 4};
        case Opcode::LDRSui: return StackPairKind{true, Opcode::LDPSi, 4};
        case Opcode::STRSui: return StackPairKind{false, Opcode::STPSi, 4};
        case Opcode::LDRXui: return StackPairKind{true, Opcode::LDPXi, 8};
        case Opcode::STRXui: return StackPairKind{false, Opcode::STPXi, 8};
        case Opcode::LDRQui: return StackPairKind{true, Opcode::LDPQi, 16};
        case Opcode::STRQui: return StackPairKind{false, Opcode::STPQi, 16};
        default: return std::nullopt;
        }
    };
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        bool paired = true;
        while (paired) {
            paired = false;
            for (auto first = instructions.begin();
                 first != instructions.end() && !paired; ++first) {
                const std::optional<StackPairKind> kind =
                    stackPairKind(first->opcode());
                if (!kind || first->operands().size() != 3 ||
                    !first->operands()[0].isPhysicalRegister() ||
                    !first->operands()[1].isPhysicalRegister() ||
                    first->operands()[2].kind() !=
                        MachineOperand::Kind::Immediate ||
                    first->memoryOperands().empty() ||
                    first->memoryOperands().front().isVolatile)
                    continue;

                const bool load = kind->load;

                const PhysReg base =
                    first->operands()[1].physicalRegister();
                if (base != PhysReg::SP && base != PhysReg::X29)
                    continue;
                const PhysReg firstData =
                    first->operands()[0].physicalRegister();
                const std::int64_t firstOffset =
                    first->operands()[2].immediate();
                for (auto second = std::next(first);
                     second != instructions.end(); ++second) {
                    const bool sameAccess =
                        second->opcode() == first->opcode();
                    bool barrier = second->isCall() ||
                                   second->isTerminator() ||
                                   (!sameAccess &&
                                    (load ? second->mayStore()
                                          : second->mayLoad() ||
                                                second->mayStore()));
                    for (const MachineOperand &operand :
                         second->operands()) {
                        if (!operand.isPhysicalRegister())
                            continue;
                        if (operand.isDef && RegisterInfo::aliases(
                                operand.physicalRegister(), base))
                            barrier = true;
                        if (!load && operand.isDef &&
                            RegisterInfo::aliases(
                                operand.physicalRegister(), firstData))
                            barrier = true;
                    }
                    if (barrier)
                        break;

                    if (!sameAccess)
                        continue;
                    if (second->operands().size() != 3 ||
                        !second->operands()[0].isPhysicalRegister() ||
                        !second->operands()[1].isPhysicalRegister() ||
                        second->operands()[2].kind() !=
                            MachineOperand::Kind::Immediate ||
                        second->operands()[1].physicalRegister() != base ||
                        second->memoryOperands().empty() ||
                        second->memoryOperands().front().isVolatile)
                        break;

                    const PhysReg secondData =
                        second->operands()[0].physicalRegister();
                    const std::int64_t secondOffset =
                        second->operands()[2].immediate();
                    std::int64_t difference = 0;
                    if (__builtin_sub_overflow(firstOffset, secondOffset,
                                               &difference) ||
                        (difference != kind->stride &&
                         difference != -kind->stride))
                        break;
                    const std::int64_t lowerOffset =
                        std::min(firstOffset, secondOffset);
                    if (lowerOffset % kind->stride != 0 ||
                        lowerOffset / kind->stride < -64 ||
                        lowerOffset / kind->stride > 63 ||
                        (load && RegisterInfo::aliases(firstData,
                                                       secondData)))
                        break;

                    // Hoisting the second load must not make its physical
                    // destination visible across an intervening use or def.
                    if (load) {
                        bool destinationHazard = false;
                        for (auto scan = std::next(first);
                             scan != second; ++scan)
                            for (const MachineOperand &operand :
                                 scan->operands())
                                if (operand.isPhysicalRegister() &&
                                    RegisterInfo::aliases(
                                        operand.physicalRegister(),
                                        secondData))
                                    destinationHazard = true;
                        if (destinationHazard)
                            continue;
                    }

                    auto lower = firstOffset < secondOffset
                                     ? first : second;
                    auto upper = firstOffset < secondOffset
                                     ? second : first;
                    MachineInstr pair(kind->pairOpcode);
                    pair.addOperand(lower->operands()[0])
                        .addOperand(upper->operands()[0])
                        .addOperand(MachineOperand::physReg(
                            base, RegClass::GPR64))
                        .addOperand(MachineOperand::immediate(
                            lowerOffset));
                    pair.addMemoryOperand(MachineMemOperand{
                        load ? MachineMemOperand::Access::Load
                             : MachineMemOperand::Access::Store,
                        static_cast<unsigned>(kind->stride * 2),
                        static_cast<unsigned>(kind->stride), nullptr,
                        std::nullopt, lowerOffset, false});

                    auto replacement = load ? first : second;
                    auto removed = load ? second : first;
                    *replacement = std::move(pair);
                    instructions.erase(removed);
                    paired = true;
                    changed = true;
                    break;
                }
            }
        }
    }

    auto postIndexedOpcode = [](Opcode opcode) {
        switch (opcode) {
        case Opcode::LDRWui: return Opcode::LDRWpost;
        case Opcode::STRWui: return Opcode::STRWpost;
        case Opcode::LDRSui: return Opcode::LDRSpost;
        case Opcode::STRSui: return Opcode::STRSpost;
        case Opcode::LDRQui: return Opcode::LDRQpost;
        case Opcode::STRQui: return Opcode::STRQpost;
        case Opcode::LDRXui: return Opcode::LDRXpost;
        case Opcode::STRXui: return Opcode::STRXpost;
        default: return Opcode::Invalid;
        }
    };

    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto memory = instructions.begin();
             memory != instructions.end(); ++memory) {
            Opcode post = postIndexedOpcode(memory->opcode());
            if (post == Opcode::Invalid ||
                memory->operands().size() != 3 ||
                !memory->operands()[1].isPhysicalRegister() ||
                memory->operands()[2].kind() !=
                    MachineOperand::Kind::Immediate ||
                memory->operands()[2].immediate() != 0)
                continue;
            PhysReg base =
                memory->operands()[1].physicalRegister();
            if (base == PhysReg::SP || base == PhysReg::X29)
                continue;

            for (auto scan = std::next(memory);
                 scan != instructions.end(); ++scan) {
                if (scan->isCall() || scan->isTerminator())
                    break;
                bool mentionsBase = false;
                for (const MachineOperand &operand :
                     scan->operands())
                    if (operand.isPhysicalRegister() &&
                        RegisterInfo::aliases(
                            operand.physicalRegister(), base)) {
                        mentionsBase = true;
                        break;
                    }
                if (!mentionsBase)
                    continue;
                bool update =
                    scan->opcode() == Opcode::ADDXri &&
                    scan->operands().size() == 3 &&
                    scan->operands()[0].isPhysicalRegister() &&
                    scan->operands()[1].isPhysicalRegister() &&
                    RegisterInfo::aliases(
                        scan->operands()[0].physicalRegister(), base) &&
                    RegisterInfo::aliases(
                        scan->operands()[1].physicalRegister(), base) &&
                    scan->operands()[2].kind() ==
                        MachineOperand::Kind::Immediate &&
                    scan->operands()[2].immediate() >= -256 &&
                    scan->operands()[2].immediate() <= 255;
                if (!update)
                    break;
                std::int64_t increment =
                    scan->operands()[2].immediate();
                memory->setOpcode(post);
                memory->operands()[1] = MachineOperand::physReg(
                    base, RegClass::GPR64, true);
                memory->operands()[2] =
                    MachineOperand::immediate(increment);
                instructions.erase(scan);
                changed = true;
                break;
            }
        }
    }
    return changed;
}

} // namespace backend::aarch64
