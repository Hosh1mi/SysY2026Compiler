#include "../../include/backend/arm64/machine_passes.hpp"
#include "../../include/backend/arm64/vector_immediate.hpp"

#include <deque>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

bool isZeroIdentity(const MachineInstr &instruction) {
    switch (instruction.opcode()) {
    case Opcode::ADDWri:
    case Opcode::SUBWri:
    case Opcode::ADDXri:
    case Opcode::SUBXri:
        return instruction.operands().size() == 3 &&
               instruction.operands()[2].kind() ==
                   MachineOperand::Kind::Immediate &&
               instruction.operands()[2].immediate() == 0;
    default:
        return false;
    }
}

bool sameRegister(const MachineOperand &lhs,
                  const MachineOperand &rhs) {
    if (lhs.isVirtualRegister() && rhs.isVirtualRegister())
        return lhs.virtualRegister() == rhs.virtualRegister();
    if (lhs.isPhysicalRegister() && rhs.isPhysicalRegister())
        return RegisterInfo::aliases(lhs.physicalRegister(),
                                     rhs.physicalRegister());
    return false;
}

bool isCallArgumentRegister(PhysReg reg) {
    return (reg >= PhysReg::X0 && reg <= PhysReg::X7) ||
           (reg >= PhysReg::V0 && reg <= PhysReg::V7);
}

bool isReturnRegister(PhysReg reg) {
    return reg == PhysReg::X0 || reg == PhysReg::V0;
}

PhysReg integerArgumentRegister(unsigned index) {
    return static_cast<PhysReg>(
        static_cast<unsigned>(PhysReg::X0) + index);
}

PhysReg vectorArgumentRegister(unsigned index) {
    return static_cast<PhysReg>(
        static_cast<unsigned>(PhysReg::V0) + index);
}

bool removableInstruction(const MachineInstr &instruction) {
    if (instruction.isTerminator() || instruction.isCall() ||
        instruction.mayLoad() || instruction.mayStore() ||
        instruction.hasSideEffects())
        return false;
    bool hasVirtualDef = false;
    for (const MachineOperand &operand : instruction.operands()) {
        if (operand.isPhysicalRegister())
            return false;
        hasVirtualDef |= operand.isVirtualRegister() && operand.isDef;
    }
    return hasVirtualDef;
}

CondCode inverseCondition(CondCode condition) {
    switch (condition) {
    case CondCode::EQ: return CondCode::NE;
    case CondCode::NE: return CondCode::EQ;
    case CondCode::HS: return CondCode::LO;
    case CondCode::LO: return CondCode::HS;
    case CondCode::MI: return CondCode::PL;
    case CondCode::PL: return CondCode::MI;
    case CondCode::VS: return CondCode::VC;
    case CondCode::VC: return CondCode::VS;
    case CondCode::HI: return CondCode::LS;
    case CondCode::LS: return CondCode::HI;
    case CondCode::GE: return CondCode::LT;
    case CondCode::LT: return CondCode::GE;
    case CondCode::GT: return CondCode::LE;
    case CondCode::LE: return CondCode::GT;
    case CondCode::AL: return CondCode::AL;
    }
    return CondCode::AL;
}

bool usesNZCV(const MachineInstr &instruction) {
    if (InstrInfo::get(instruction.opcode()).usesFlags)
        return true;
    for (const MachineOperand &operand : instruction.operands())
        if (operand.isPhysicalRegister() && !operand.isDef &&
            operand.physicalRegister() == PhysReg::NZCV)
            return true;
    return false;
}

bool definesNZCV(const MachineInstr &instruction) {
    if (InstrInfo::get(instruction.opcode()).setsFlags ||
        instruction.isCall())
        return true;
    for (const MachineOperand &operand : instruction.operands())
        if (operand.isPhysicalRegister() && operand.isDef &&
            operand.physicalRegister() == PhysReg::NZCV)
            return true;
    return false;
}

bool flagsUsedAfter(
    MachineBasicBlock *block,
    MachineBasicBlock::InstrList::const_iterator begin) {
    auto inspectRange = [](auto first, auto last) {
        for (; first != last; ++first) {
            // An instruction that both reads and writes flags consumes the
            // incoming value before defining the outgoing one.
            if (usesNZCV(*first))
                return 1;
            if (definesNZCV(*first))
                return -1;
        }
        return 0;
    };

    int local = inspectRange(begin, block->instructions().end());
    if (local != 0)
        return local > 0;

    std::deque<MachineBasicBlock *> worklist;
    std::unordered_set<MachineBasicBlock *> visited;
    for (MachineBasicBlock *successor : block->successors())
        if (successor && visited.insert(successor).second)
            worklist.push_back(successor);

    while (!worklist.empty()) {
        MachineBasicBlock *current = worklist.front();
        worklist.pop_front();
        int access = inspectRange(
            current->instructions().begin(),
            current->instructions().end());
        if (access > 0)
            return true;
        if (access < 0)
            continue;
        for (MachineBasicBlock *successor :
             current->successors())
            if (successor && visited.insert(successor).second)
                worklist.push_back(successor);
    }
    return false;
}

} // namespace

bool PreRAMachinePeephole::run(MachineFunction &function) const {
    bool changed = false;
    // Local Machine CSE for materialized constants.  Keeping this before RA
    // lets graph coloring decide whether the shared value belongs in a
    // caller-saved register, a callee-saved register, or a spill slot.  The
    // table is cleared at calls so this transformation does not manufacture
    // a new live-across-call range.
    for (auto &block : function.blocks()) {
        std::unordered_map<std::string, VReg> available;
        auto &instructions = block->instructions();
        for (auto it = instructions.begin();
             it != instructions.end();) {
            if (it->isCall()) {
                available.clear();
                ++it;
                continue;
            }
            bool cseCandidate =
                it->opcode() == Opcode::MOVi32 ||
                it->opcode() == Opcode::MOVi64 ||
                it->opcode() == Opcode::MOVIv4Zero ||
                it->opcode() == Opcode::MOVIv4s ||
                it->opcode() == Opcode::MOVIv4sMsl ||
                it->opcode() == Opcode::MVNIv4s ||
                it->opcode() == Opcode::MOVIv16b ||
                it->opcode() == Opcode::FMOVv4s ||
                it->opcode() == Opcode::DUPv4i32 ||
                it->opcode() == Opcode::DUPv4f32;
            if (!cseCandidate || it->operands().empty() ||
                !it->operands()[0].isVirtualRegister() ||
                !it->operands()[0].isDef) {
                ++it;
                continue;
            }

            std::string key =
                std::to_string(static_cast<unsigned>(it->opcode()));
            if (it->opcode() == Opcode::MOVIv4Zero) {
                // zero vector has no immediate payload
            } else if (it->opcode() == Opcode::DUPv4i32 ||
                       it->opcode() == Opcode::DUPv4f32) {
                if (it->operands().size() != 2 ||
                    !it->operands()[1].isVirtualRegister()) {
                    ++it;
                    continue;
                }
                key += ":v" + std::to_string(
                    it->operands()[1].virtualRegister());
            } else if (it->opcode() == Opcode::FMOVv4s) {
                if (it->operands().size() != 2 ||
                    it->operands()[1].kind() !=
                        MachineOperand::Kind::FloatingBits) {
                    ++it;
                    continue;
                }
                key += ":" +
                       std::to_string(it->operands()[1].floatingBits());
            } else if (it->opcode() == Opcode::MOVIv4s ||
                       it->opcode() == Opcode::MOVIv4sMsl ||
                       it->opcode() == Opcode::MVNIv4s) {
                if (it->operands().size() != 3 ||
                    it->operands()[1].kind() !=
                        MachineOperand::Kind::Immediate ||
                    it->operands()[2].kind() !=
                        MachineOperand::Kind::Immediate) {
                    ++it;
                    continue;
                }
                key += ":" +
                       std::to_string(it->operands()[1].immediate()) +
                       ":" +
                       std::to_string(it->operands()[2].immediate());
            } else {
                if (it->operands().size() != 2 ||
                    it->operands()[1].kind() !=
                        MachineOperand::Kind::Immediate) {
                    ++it;
                    continue;
                }
                key += ":" +
                       std::to_string(
                           it->operands()[1].immediate());
            }
            VReg duplicate =
                it->operands()[0].virtualRegister();
            auto canonical = available.find(key);
            if (canonical == available.end()) {
                available.emplace(std::move(key), duplicate);
                ++it;
                continue;
            }

            VReg replacement = canonical->second;
            for (auto &useBlock : function.blocks())
                for (MachineInstr &instruction :
                     useBlock->instructions())
                    for (MachineOperand &operand :
                         instruction.operands())
                        if (operand.isVirtualRegister() &&
                            !operand.isDef &&
                            operand.virtualRegister() ==
                                duplicate)
                            operand = MachineOperand::vreg(
                                replacement,
                                operand.regClass());
            it = instructions.erase(it);
            function.registerInfo().eraseVirtualRegister(
                duplicate);
            changed = true;
        }
    }

    // GPR vs NEON bank choice for splat immediates.
    //
    // ISel keeps Splat as DUP from a scalar so integer users can share the
    // same MOVi.  When that scalar immediate has no non-broadcast users,
    // rewrite the DUP into a NEON immediate.  Dead scalar materializations
    // are left for Machine DCE; Machine LICM then places the NEON form.
    {
        std::unordered_map<VReg, unsigned> useCount;
        for (const auto &block : function.blocks())
            for (const MachineInstr &instruction : block->instructions())
                for (const MachineOperand &operand : instruction.operands())
                    if (operand.isVirtualRegister() && !operand.isDef)
                        ++useCount[operand.virtualRegister()];

        auto definitionOf = [&](VReg reg) -> MachineInstr * {
            if (!function.registerInfo().contains(reg))
                return nullptr;
            return function.registerInfo().get(reg).definition;
        };

        auto onlyUsedAs = [&](VReg reg, Opcode opcode) {
            if (!useCount.count(reg) || useCount[reg] == 0)
                return false;
            unsigned seen = 0;
            for (const auto &block : function.blocks())
                for (const MachineInstr &instruction :
                     block->instructions())
                    for (const MachineOperand &operand :
                         instruction.operands()) {
                        if (!operand.isVirtualRegister() ||
                            operand.isDef ||
                            operand.virtualRegister() != reg)
                            continue;
                        if (instruction.opcode() != opcode)
                            return false;
                        ++seen;
                    }
            return seen == useCount[reg];
        };

        for (auto &block : function.blocks()) {
            auto &instructions = block->instructions();
            for (auto it = instructions.begin();
                 it != instructions.end(); ++it) {
                const bool integerDup = it->opcode() == Opcode::DUPv4i32;
                const bool floatDup = it->opcode() == Opcode::DUPv4f32;
                if ((!integerDup && !floatDup) ||
                    it->operands().size() != 2 ||
                    !it->operands()[0].isVirtualRegister() ||
                    !it->operands()[0].isDef ||
                    !it->operands()[1].isVirtualRegister())
                    continue;

                VReg scalar = it->operands()[1].virtualRegister();
                MachineInstr *scalarDef = definitionOf(scalar);
                std::uint32_t bits = 0;
                if (integerDup) {
                    if (!scalarDef ||
                        scalarDef->opcode() != Opcode::MOVi32 ||
                        scalarDef->operands().size() != 2 ||
                        scalarDef->operands()[1].kind() !=
                            MachineOperand::Kind::Immediate ||
                        !onlyUsedAs(scalar, Opcode::DUPv4i32))
                        continue;
                    bits = static_cast<std::uint32_t>(
                        scalarDef->operands()[1].immediate());
                } else {
                    // Prefer FMOVSW -> DUP when the FPR value is broadcast
                    // only.  Walk through an optional MOVi32 of the bit
                    // pattern so float splats share the same policy.
                    if (!scalarDef ||
                        scalarDef->opcode() != Opcode::FMOVSW ||
                        scalarDef->operands().size() != 2 ||
                        !scalarDef->operands()[1].isVirtualRegister() ||
                        !onlyUsedAs(scalar, Opcode::DUPv4f32))
                        continue;
                    VReg bitsReg =
                        scalarDef->operands()[1].virtualRegister();
                    MachineInstr *bitsDef = definitionOf(bitsReg);
                    if (!bitsDef || bitsDef->opcode() != Opcode::MOVi32 ||
                        bitsDef->operands().size() != 2 ||
                        bitsDef->operands()[1].kind() !=
                            MachineOperand::Kind::Immediate ||
                        !onlyUsedAs(bitsReg, Opcode::FMOVSW))
                        continue;
                    bits = static_cast<std::uint32_t>(
                        bitsDef->operands()[1].immediate());
                }

                auto immediate = classifyNeonSplatImmediate(bits);
                if (!immediate)
                    continue;
                MachineOperand destination = it->operands()[0];
                *it = makeNeonSplatImmediate(*immediate, destination);
                if (destination.isVirtualRegister())
                    function.registerInfo().setDefinition(
                        destination.virtualRegister(), &*it);
                changed = true;
            }
        }
    }

    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (isZeroIdentity(*it)) {
                MachineOperand destination = it->operands()[0];
                MachineOperand source = it->operands()[1];
                it->setOpcode(Opcode::COPY);
                it->operands().clear();
                it->addOperand(std::move(destination))
                    .addOperand(std::move(source));
                changed = true;
            }
            if (it->opcode() == Opcode::COPY &&
                it->operands().size() == 2 &&
                sameRegister(it->operands()[0], it->operands()[1]) &&
                it->operands()[0].isPhysicalRegister()) {
                it = instructions.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }
    }

    // Pair adjacent same-width LDR/STR into LDP/STP.  Element size is the
    // architectural scale (4/8/16): the pair offset must be a multiple of that
    // scale and fit the signed 7-bit scaled immediate.
    struct AddressForm {
        VReg root = 0;
        std::int64_t offset = 0;
        bool valid = false;
    };
    struct PairKind {
        Opcode loadOpcode;
        Opcode storeOpcode;
        Opcode pairLoadOpcode;
        Opcode pairStoreOpcode;
        std::int64_t stride;
    };
    static const PairKind kPairKinds[] = {
        {Opcode::LDRWui, Opcode::STRWui, Opcode::LDPWi, Opcode::STPWi, 4},
        {Opcode::LDRSui, Opcode::STRSui, Opcode::LDPSi, Opcode::STPSi, 4},
        {Opcode::LDRXui, Opcode::STRXui, Opcode::LDPXi, Opcode::STPXi, 8},
        {Opcode::LDRQui, Opcode::STRQui, Opcode::LDPQi, Opcode::STPQi, 16},
    };
    auto definesVirtual = [](const MachineInstr &instruction, VReg reg) {
        if (!reg)
            return false;
        for (const MachineOperand &operand : instruction.operands())
            if (operand.isVirtualRegister() && operand.isDef &&
                operand.virtualRegister() == reg)
                return true;
        return false;
    };
    for (auto &block : function.blocks()) {
        bool fused = true;
        while (fused) {
            fused = false;
            auto &instructions = block->instructions();
            std::unordered_map<VReg, AddressForm> addresses;
            auto addressOf = [&](const MachineOperand &operand) {
                AddressForm form;
                if (!operand.isVirtualRegister() ||
                    operand.regClass() != RegClass::GPR64)
                    return form;
                auto found =
                    addresses.find(operand.virtualRegister());
                if (found != addresses.end())
                    return found->second;
                form.root = operand.virtualRegister();
                form.valid = true;
                return form;
            };

            struct MemoryCandidate {
                MachineBasicBlock::InstrList::iterator instruction;
                AddressForm address;
                const PairKind *kind = nullptr;
            };
            std::vector<MemoryCandidate> loads;
            std::vector<MemoryCandidate> stores;
            for (auto it = instructions.begin();
                 it != instructions.end(); ++it) {
                if (it->opcode() == Opcode::COPY &&
                    it->operands().size() == 2 &&
                    it->operands()[0].isVirtualRegister() &&
                    it->operands()[0].regClass() ==
                        RegClass::GPR64) {
                    AddressForm form =
                        addressOf(it->operands()[1]);
                    if (form.valid)
                        addresses[
                            it->operands()[0]
                                .virtualRegister()] = form;
                } else if (
                    it->opcode() == Opcode::ADDXri &&
                    it->operands().size() == 3 &&
                    it->operands()[0].isVirtualRegister() &&
                    it->operands()[2].kind() ==
                        MachineOperand::Kind::Immediate) {
                    AddressForm form =
                        addressOf(it->operands()[1]);
                    if (form.valid) {
                        form.offset +=
                            it->operands()[2].immediate();
                        addresses[
                            it->operands()[0]
                                .virtualRegister()] = form;
                    }
                }

                const PairKind *kind = nullptr;
                bool load = false;
                bool store = false;
                for (const PairKind &candidate : kPairKinds) {
                    if (it->opcode() == candidate.loadOpcode) {
                        kind = &candidate;
                        load = true;
                        break;
                    }
                    if (it->opcode() == candidate.storeOpcode) {
                        kind = &candidate;
                        store = true;
                        break;
                    }
                }
                if ((!load && !store) ||
                    it->operands().size() != 3 ||
                    it->operands()[2].kind() !=
                        MachineOperand::Kind::Immediate ||
                    it->memoryOperands().empty() ||
                    it->memoryOperands().front().isVolatile)
                    continue;
                AddressForm form =
                    addressOf(it->operands()[1]);
                if (!form.valid)
                    continue;
                form.offset +=
                    it->operands()[2].immediate();
                (load ? loads : stores).push_back(
                    MemoryCandidate{it, form, kind});
            }

            auto tryPair = [&](std::vector<MemoryCandidate> &candidates,
                               bool load) {
                for (std::size_t i = 0;
                     i < candidates.size(); ++i) {
                    for (std::size_t j = i + 1;
                         j < candidates.size(); ++j) {
                        auto &lhs = candidates[i];
                        auto &rhs = candidates[j];
                        if (lhs.kind != rhs.kind ||
                            lhs.address.root !=
                                rhs.address.root ||
                            std::llabs(
                                lhs.address.offset -
                                rhs.address.offset) !=
                                lhs.kind->stride)
                            continue;
                        std::int64_t stride = lhs.kind->stride;
                        std::int64_t lowerOffset =
                            std::min(lhs.address.offset,
                                     rhs.address.offset);
                        if (lowerOffset % stride != 0 ||
                            lowerOffset / stride < -64 ||
                            lowerOffset / stride > 63)
                            continue;

                        // Loads are rewritten at the earlier instruction so
                        // both values become available without moving uses.
                        // Stores must stay at the later instruction so both
                        // data operands are already defined.
                        VReg earlyData = 0;
                        if (!load &&
                            lhs.instruction->operands()[0]
                                .isVirtualRegister())
                            earlyData = lhs.instruction->operands()[0]
                                            .virtualRegister();

                        bool memoryBarrier = false;
                        for (auto scan =
                                 std::next(lhs.instruction);
                             scan != rhs.instruction; ++scan) {
                            if (scan->isCall() ||
                                scan->mayStore() ||
                                (!load &&
                                 scan->mayLoad()) ||
                                definesVirtual(*scan,
                                               lhs.address.root) ||
                                (!load &&
                                 definesVirtual(*scan,
                                                earlyData))) {
                                memoryBarrier = true;
                                break;
                            }
                        }
                        if (memoryBarrier)
                            continue;

                        // LDP with Rt == Rt2 is unpredictable.
                        if (load &&
                            sameRegister(lhs.instruction->operands()[0],
                                         rhs.instruction->operands()[0]))
                            continue;

                        auto lower =
                            lhs.address.offset < rhs.address.offset
                                ? lhs.instruction
                                : rhs.instruction;
                        auto upper =
                            lhs.address.offset < rhs.address.offset
                                ? rhs.instruction
                                : lhs.instruction;
                        MachineInstr pair(
                            load ? lhs.kind->pairLoadOpcode
                                 : lhs.kind->pairStoreOpcode);
                        pair.addOperand(
                                lower->operands()[0])
                            .addOperand(
                                upper->operands()[0])
                            .addOperand(
                                MachineOperand::vreg(
                                    lhs.address.root,
                                    RegClass::GPR64))
                            .addOperand(
                                MachineOperand::immediate(
                                    lowerOffset));
                        unsigned pairBytes =
                            static_cast<unsigned>(stride * 2);
                        unsigned align =
                            static_cast<unsigned>(stride);
                        pair.addMemoryOperand(
                            MachineMemOperand{
                                load
                                    ? MachineMemOperand::Access::
                                          Load
                                    : MachineMemOperand::Access::
                                          Store,
                                pairBytes, align, nullptr,
                                std::nullopt, lowerOffset,
                                false});

                        auto replacement =
                            load ? lhs.instruction
                                 : rhs.instruction;
                        auto removed =
                            load ? rhs.instruction
                                 : lhs.instruction;
                        *replacement = std::move(pair);
                        if (load) {
                            for (unsigned operand = 0;
                                 operand < 2; ++operand)
                                if (replacement->operands()[
                                        operand]
                                        .isVirtualRegister())
                                    function.registerInfo()
                                        .setDefinition(
                                            replacement
                                                ->operands()[
                                                    operand]
                                                .virtualRegister(),
                                            &*replacement);
                        }
                        instructions.erase(removed);
                        return true;
                    }
                }
                return false;
            };

            fused = tryPair(loads, true);
            if (!fused)
                fused = tryPair(stores, false);
            changed |= fused;
        }
    }

    if (!function.hasProperty(MachineProperty::HasPHIs)) {
        auto sinkableOpcode = [](Opcode opcode) {
            switch (opcode) {
            case Opcode::MOVi32:
            case Opcode::MOVi64:
            case Opcode::MOVIv4Zero:
            case Opcode::FMOVWS:
            case Opcode::FMOVSW:
            case Opcode::COPYXtoW:
            case Opcode::SXTW:
            case Opcode::UXTW:
                return true;
            default:
                return false;
            }
        };

        // Sink a single-use, side-effect-free materialization through a
        // single-predecessor edge.  This is deliberately narrower than
        // general code sinking: the edge proves availability and prevents a
        // PHI or alternate predecessor from observing an undefined value.
        bool sunk = true;
        while (sunk) {
            sunk = false;
            std::unordered_map<VReg, unsigned> useCount;
            std::unordered_map<VReg, MachineBasicBlock *> useBlock;
            std::unordered_map<VReg, MachineInstr *> useInstruction;
            for (auto &owned : function.blocks())
                for (MachineInstr &instruction :
                     owned->instructions())
                    for (const MachineOperand &operand :
                         instruction.operands())
                        if (operand.isVirtualRegister() &&
                            !operand.isDef) {
                            VReg reg = operand.virtualRegister();
                            ++useCount[reg];
                            useBlock[reg] = owned.get();
                            useInstruction[reg] = &instruction;
                        }

            for (auto &owned : function.blocks()) {
                MachineBasicBlock *source = owned.get();
                auto &sourceInstructions =
                    source->instructions();
                for (auto instruction =
                         sourceInstructions.begin();
                     instruction != sourceInstructions.end();
                     ++instruction) {
                    if (!sinkableOpcode(
                            instruction->opcode()) ||
                        instruction->isTerminator() ||
                        instruction->isCall() ||
                        instruction->mayLoad() ||
                        instruction->mayStore() ||
                        instruction->hasSideEffects())
                        continue;
                    VReg definition = 0;
                    bool valid = true;
                    for (const MachineOperand &operand :
                         instruction->operands()) {
                        if (operand.isPhysicalRegister()) {
                            valid = false;
                            break;
                        }
                        if (!operand.isVirtualRegister() ||
                            !operand.isDef)
                            continue;
                        if (definition) {
                            valid = false;
                            break;
                        }
                        definition =
                            operand.virtualRegister();
                    }
                    if (!valid || !definition ||
                        useCount[definition] != 1)
                        continue;
                    MachineBasicBlock *destination =
                        useBlock[definition];
                    if (!destination ||
                        destination == source ||
                        destination->predecessors().size() != 1 ||
                        destination->predecessors().front() !=
                            source)
                        continue;

                    auto &destinationInstructions =
                        destination->instructions();
                    auto insertion = std::find_if(
                        destinationInstructions.begin(),
                        destinationInstructions.end(),
                        [&](const MachineInstr &candidate) {
                            return &candidate ==
                                   useInstruction[definition];
                        });
                    if (insertion ==
                        destinationInstructions.end())
                        continue;
                    destinationInstructions.splice(
                        insertion, sourceInstructions,
                        instruction);
                    function.clearProperty(
                        MachineProperty::TracksLiveness);
                    changed = true;
                    sunk = true;
                    break;
                }
                if (sunk)
                    break;
            }
        }
    }
    return changed;
}

bool DeadMachineInstructionElimination::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::IsSSA))
        return false;

    std::unordered_map<VReg, unsigned> uses;
    std::unordered_map<VReg, MachineInstr *> definition;
    for (auto &block : function.blocks()) {
        for (MachineInstr &instruction : block->instructions()) {
            for (const MachineOperand &operand : instruction.operands()) {
                if (!operand.isVirtualRegister())
                    continue;
                if (operand.isDef)
                    definition[operand.virtualRegister()] = &instruction;
                else
                    ++uses[operand.virtualRegister()];
            }
        }
    }

    std::deque<VReg> worklist;
    for (const auto &[reg, instruction] : definition)
        if (!uses[reg])
            worklist.push_back(reg);

    std::unordered_set<MachineInstr *> dead;
    while (!worklist.empty()) {
        VReg reg = worklist.front();
        worklist.pop_front();
        auto found = definition.find(reg);
        if (found == definition.end())
            continue;
        MachineInstr *instruction = found->second;
        if (dead.count(instruction) ||
            !removableInstruction(*instruction))
            continue;

        bool allDefsDead = true;
        for (const MachineOperand &operand : instruction->operands())
            if (operand.isVirtualRegister() && operand.isDef &&
                uses[operand.virtualRegister()] != 0) {
                allDefsDead = false;
                break;
            }
        if (!allDefsDead)
            continue;

        dead.insert(instruction);
        for (const MachineOperand &operand : instruction->operands()) {
            if (!operand.isVirtualRegister() || operand.isDef)
                continue;
            VReg used = operand.virtualRegister();
            if (uses[used] && --uses[used] == 0)
                worklist.push_back(used);
        }
    }

    if (dead.empty())
        return false;
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (!dead.count(&*it)) {
                ++it;
                continue;
            }
            for (const MachineOperand &operand : it->operands())
                if (operand.isVirtualRegister() && operand.isDef)
                    function.registerInfo().eraseVirtualRegister(
                        operand.virtualRegister());
            it = instructions.erase(it);
        }
    }
    function.clearProperty(MachineProperty::TracksLiveness);
    return true;
}

bool MachineLICM::run(MachineFunction &function) const {
    if (function.blocks().empty())
        return false;

    using BlockSet = std::unordered_set<MachineBasicBlock *>;
    std::vector<MachineBasicBlock *> blocks;
    for (const auto &block : function.blocks())
        blocks.push_back(block.get());
    std::unordered_map<MachineBasicBlock *, BlockSet> dominators;
    for (MachineBasicBlock *block : blocks) {
        if (block == function.entryBlock())
            dominators[block].insert(block);
        else
            dominators[block].insert(blocks.begin(), blocks.end());
    }
    bool domChanged = true;
    while (domChanged) {
        domChanged = false;
        for (MachineBasicBlock *block : blocks) {
            if (block == function.entryBlock())
                continue;
            BlockSet next;
            bool first = true;
            for (MachineBasicBlock *predecessor :
                 block->predecessors()) {
                if (first) {
                    next = dominators[predecessor];
                    first = false;
                    continue;
                }
                for (auto it = next.begin(); it != next.end();) {
                    if (!dominators[predecessor].count(*it))
                        it = next.erase(it);
                    else
                        ++it;
                }
            }
            next.insert(block);
            if (next != dominators[block]) {
                dominators[block] = std::move(next);
                domChanged = true;
            }
        }
    }

    struct Loop {
        MachineBasicBlock *header = nullptr;
        MachineBasicBlock *preheader = nullptr;
        BlockSet blocks;
    };
    std::vector<Loop> loops;
    for (MachineBasicBlock *tail : blocks) {
        for (MachineBasicBlock *header : tail->successors()) {
            if (!dominators[tail].count(header))
                continue;
            Loop loop;
            loop.header = header;
            loop.blocks.insert(header);
            loop.blocks.insert(tail);
            std::vector<MachineBasicBlock *> worklist = {tail};
            while (!worklist.empty()) {
                MachineBasicBlock *current = worklist.back();
                worklist.pop_back();
                for (MachineBasicBlock *predecessor :
                     current->predecessors())
                    if (loop.blocks.insert(predecessor).second &&
                        predecessor != header)
                        worklist.push_back(predecessor);
            }
            std::vector<MachineBasicBlock *> outside;
            for (MachineBasicBlock *predecessor :
                 header->predecessors())
                if (!loop.blocks.count(predecessor))
                    outside.push_back(predecessor);
            if (outside.size() == 1 &&
                outside.front()->successors().size() == 1)
                loop.preheader = outside.front();
            if (loop.preheader)
                loops.push_back(std::move(loop));
        }
    }
    std::sort(loops.begin(), loops.end(),
              [](const Loop &lhs, const Loop &rhs) {
                  return lhs.blocks.size() < rhs.blocks.size();
              });

    std::unordered_map<VReg, MachineBasicBlock *> definitionBlock;
    for (MachineBasicBlock *block : blocks)
        for (MachineInstr &instruction : block->instructions())
            for (const MachineOperand &operand :
                 instruction.operands())
                if (operand.isVirtualRegister() && operand.isDef)
                    definitionBlock[operand.virtualRegister()] = block;

    auto hoistableOpcode = [](Opcode opcode) {
        switch (opcode) {
        case Opcode::MOVi32:
        case Opcode::MOVi64:
            // Scalar immediates rematerialize cheaply, so lengthening their
            // live ranges is fine.  Encoded NEON immediates (including
            // movi #0) must stay near their uses: hoisting them occupies a
            // vector register across nested loops and invites spill churn.
            return true;
        default:
            return false;
        }
    };

    bool changed = false;
    std::unordered_set<MachineInstr *> hoistedThisRun;
    std::unordered_set<MachineBasicBlock *> nestedPreheaders;
    for (const Loop &loop : loops)
        nestedPreheaders.insert(loop.preheader);
    for (Loop &loop : loops) {
        auto insertion = std::find_if(
            loop.preheader->instructions().begin(),
            loop.preheader->instructions().end(),
            [](const MachineInstr &instruction) {
                return instruction.isTerminator();
            });
        bool localChange = true;
        while (localChange) {
            localChange = false;
            for (MachineBasicBlock *block : blocks) {
                if (!loop.blocks.count(block))
                    continue;
                auto &instructions = block->instructions();
                for (auto it = instructions.begin();
                     it != instructions.end();) {
                    auto current = it++;
                    if (!hoistableOpcode(current->opcode()))
                        continue;
                    if (hoistedThisRun.count(&*current))
                        continue;
                    // A nested loop preheader is already the profitable
                    // placement for cheap rematerializable constants.  Do
                    // not repeatedly lift them through surrounding loops,
                    // which needlessly lengthens live ranges and can evict
                    // the actual induction state.
                    if (nestedPreheaders.count(block) &&
                        block != loop.header)
                        continue;
                    bool invariant = true;
                    for (const MachineOperand &operand :
                         current->operands()) {
                        if (operand.isPhysicalRegister()) {
                            invariant = false;
                            break;
                        }
                        if (!operand.isVirtualRegister() ||
                            operand.isDef)
                            continue;
                        auto definition = definitionBlock.find(
                            operand.virtualRegister());
                        if (definition != definitionBlock.end() &&
                            loop.blocks.count(definition->second)) {
                            invariant = false;
                            break;
                        }
                    }
                    if (!invariant)
                        continue;
                    for (const MachineOperand &operand :
                         current->operands())
                        if (operand.isVirtualRegister() &&
                            operand.isDef)
                            definitionBlock[operand.virtualRegister()] =
                                loop.preheader;
                    loop.preheader->instructions().splice(
                        insertion, instructions, current);
                    hoistedThisRun.insert(&*current);
                    localChange = true;
                    changed = true;
                }
            }
        }
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

bool AArch64ConditionOptimizer::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::IsSSA))
        return false;
    std::unordered_map<VReg, unsigned> useCount;
    for (const auto &block : function.blocks())
        for (const MachineInstr &instruction : block->instructions())
            for (const MachineOperand &operand :
                 instruction.operands())
                if (operand.isVirtualRegister() && !operand.isDef)
                    ++useCount[operand.virtualRegister()];

    bool changed = false;

    // Preserve the original compare flags through an integerized boolean
    // when that boolean is used only to drive a conditional select:
    //
    //   NZCV = cmp a, b
    //   %p   = cset cc, NZCV
    //          ... instructions that do not define NZCV ...
    //   NZCV = cmp %p, 0
    //   %r   = csel %t, %f, ne, NZCV
    //
    // becomes a csel using `cc` and the first NZCV definition.  This is the
    // same flags-glue selection LLVM performs before RA; it removes no
    // observable comparison and introduces no physical scratch register.
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto set = instructions.begin();
             set != instructions.end();) {
            if (set->opcode() != Opcode::CSETW ||
                set->operands().size() < 2 ||
                !set->operands()[0].isVirtualRegister() ||
                !set->operands()[0].isDef) {
                ++set;
                continue;
            }
            VReg conditionReg =
                set->operands()[0].virtualRegister();
            if (useCount[conditionReg] != 1) {
                ++set;
                continue;
            }

            auto booleanCompare = std::next(set);
            auto isBooleanCompare = [&](const MachineInstr &instruction) {
                return instruction.opcode() == Opcode::CMPWri &&
                       instruction.operands().size() >= 2 &&
                       instruction.operands()[0].isVirtualRegister() &&
                       instruction.operands()[0].virtualRegister() ==
                           conditionReg &&
                       instruction.operands()[1].kind() ==
                           MachineOperand::Kind::Immediate &&
                       instruction.operands()[1].immediate() == 0;
            };
            while (booleanCompare != instructions.end()) {
                if (isBooleanCompare(*booleanCompare))
                    break;
                const InstrDesc &descriptor =
                    InstrInfo::get(booleanCompare->opcode());
                bool clobbersFlags =
                    descriptor.setsFlags ||
                    booleanCompare->isCall();
                for (const MachineOperand &operand :
                     booleanCompare->operands())
                    clobbersFlags |=
                        operand.isPhysicalRegister() &&
                        operand.isDef &&
                        operand.physicalRegister() ==
                            PhysReg::NZCV;
                if (clobbersFlags ||
                    booleanCompare->isTerminator())
                    break;
                ++booleanCompare;
            }
            if (booleanCompare == instructions.end() ||
                !isBooleanCompare(*booleanCompare)) {
                ++set;
                continue;
            }

            CondCode originalCondition =
                set->operands()[1].condition();
            bool rewroteSelect = false;
            for (auto select = std::next(booleanCompare);
                 select != instructions.end();) {
                bool candidate =
                    (select->opcode() == Opcode::CSELW ||
                     select->opcode() == Opcode::CSELX ||
                     select->opcode() == Opcode::FCSELS) &&
                    select->operands().size() >= 4 &&
                    select->operands()[3].kind() ==
                        MachineOperand::Kind::ConditionCode;
                if (candidate) {
                    CondCode booleanCondition =
                        select->operands()[3].condition();
                    if (booleanCondition != CondCode::NE &&
                        booleanCondition != CondCode::EQ)
                        break;
                    select->operands()[3] = MachineOperand::condition(
                        booleanCondition == CondCode::NE
                            ? originalCondition
                            : inverseCondition(originalCondition));
                    rewroteSelect = true;
                    ++select;
                    continue;
                }
                const InstrDesc &descriptor = InstrInfo::get(select->opcode());
                if (descriptor.setsFlags || descriptor.usesFlags ||
                    select->isCall() || select->isTerminator())
                    break;
                ++select;
            }
            if (!rewroteSelect) {
                ++set;
                continue;
            }
            instructions.erase(booleanCompare);
            set = instructions.erase(set);
            function.registerInfo().eraseVirtualRegister(
                conditionReg);
            changed = true;
        }
    }

    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (it->opcode() != Opcode::CSETW ||
                it->operands().size() < 2 ||
                !it->operands()[0].isVirtualRegister() ||
                !it->operands()[0].isDef) {
                ++it;
                continue;
            }
            VReg conditionReg =
                it->operands()[0].virtualRegister();
            auto branch = std::next(it);
            if (useCount[conditionReg] != 1) {
                ++it;
                continue;
            }
            while (branch != instructions.end()) {
                const InstrDesc &descriptor =
                    InstrInfo::get(branch->opcode());
                bool clobbersFlags = descriptor.setsFlags ||
                    branch->isCall();
                for (const MachineOperand &operand :
                     branch->operands())
                    clobbersFlags |=
                        operand.isPhysicalRegister() &&
                        operand.isDef &&
                        operand.physicalRegister() ==
                            PhysReg::NZCV;
                if (clobbersFlags)
                    break;
                bool candidate =
                    (branch->opcode() == Opcode::CBNZ ||
                     branch->opcode() == Opcode::CBZ) &&
                    branch->operands().size() == 2 &&
                    branch->operands()[0].isVirtualRegister() &&
                    branch->operands()[0].virtualRegister() ==
                        conditionReg;
                if (candidate)
                    break;
                if (branch->isTerminator())
                    break;
                ++branch;
            }
            if (branch == instructions.end() ||
                (branch->opcode() != Opcode::CBNZ &&
                 branch->opcode() != Opcode::CBZ) ||
                branch->operands().size() != 2 ||
                !branch->operands()[0].isVirtualRegister() ||
                branch->operands()[0].virtualRegister() !=
                    conditionReg) {
                ++it;
                continue;
            }

            CondCode condition = it->operands()[1].condition();
            if (branch->opcode() == Opcode::CBZ)
                condition = inverseCondition(condition);
            MachineBasicBlock *target =
                branch->operands()[1].basicBlock();
            branch->setOpcode(Opcode::Bcc);
            branch->operands().clear();
            branch->addOperand(
                      MachineOperand::condition(condition))
                .addOperand(MachineOperand::block(target))
                .addOperand(MachineOperand::physReg(
                    PhysReg::NZCV, RegClass::CCR, false, true));
            function.registerInfo().eraseVirtualRegister(
                conditionReg);
            it = instructions.erase(it);
            changed = true;
        }
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

bool PreRACFGOptimizer::run(MachineFunction &function) const {
    if (function.hasProperty(MachineProperty::NoVRegs))
        return false;

    auto sameVReg = [](const MachineOperand &lhs,
                       const MachineOperand &rhs) {
        return lhs.isVirtualRegister() && rhs.isVirtualRegister() &&
               lhs.virtualRegister() == rhs.virtualRegister();
    };

    bool changed = false;

    struct MaterializedI32 {
        MachineInstr *instruction = nullptr;
        VReg reg = 0;
        std::uint32_t value = 0;
    };
    auto materializedI32 =
        [&](const MachineOperand &operand)
            -> std::optional<MaterializedI32> {
        if (!operand.isVirtualRegister() ||
            !function.registerInfo().contains(
                operand.virtualRegister()))
            return std::nullopt;
        const VRegInfo &info = function.registerInfo().get(
            operand.virtualRegister());
        MachineInstr *definition = info.definition;
        if (!definition ||
            definition->opcode() != Opcode::MOVi32 ||
            definition->operands().size() != 2 ||
            !sameVReg(definition->operands()[0], operand) ||
            definition->operands()[1].kind() !=
                MachineOperand::Kind::Immediate)
            return std::nullopt;
        return MaterializedI32{
            definition, operand.virtualRegister(),
            static_cast<std::uint32_t>(
                definition->operands()[1].immediate())};
    };
    auto eraseInstruction = [&](MachineInstr *target) {
        if (!target)
            return;
        for (auto &owned : function.blocks()) {
            auto &instructions = owned->instructions();
            auto found = std::find_if(
                instructions.begin(), instructions.end(),
                [&](const MachineInstr &instruction) {
                    return &instruction == target;
                });
            if (found != instructions.end()) {
                instructions.erase(found);
                return;
            }
        }
    };

    // Fold an AND whose result only feeds an equality-to-zero branch.
    // One-bit masks use TBZ/TBNZ; encodable constants use TST-immediate;
    // all other masks use TST-register.  The transformation is restricted to
    // EQ/NE and rejects live-out flags because TST does not reproduce every
    // CMP flag.
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        bool localChange = true;
        while (localChange) {
            localChange = false;
            std::unordered_map<VReg, unsigned> useCount;
            for (const auto &useBlock : function.blocks())
                for (const MachineInstr &instruction :
                     useBlock->instructions())
                    for (const MachineOperand &operand :
                         instruction.operands())
                        if (operand.isVirtualRegister() &&
                            !operand.isDef)
                            ++useCount[operand.virtualRegister()];

            for (auto bitAnd = instructions.begin();
                 bitAnd != instructions.end(); ++bitAnd) {
                if ((bitAnd->opcode() != Opcode::ANDWrr &&
                     bitAnd->opcode() != Opcode::ANDWri) ||
                    bitAnd->operands().size() != 3 ||
                    !bitAnd->operands()[0].isVirtualRegister())
                    continue;

                VReg extracted =
                    bitAnd->operands()[0].virtualRegister();
                if (useCount[extracted] != 1)
                    continue;

                auto compare = std::next(bitAnd);
                if (compare == instructions.end() ||
                    compare->opcode() != Opcode::CMPWri ||
                    compare->operands().size() < 2 ||
                    !sameVReg(compare->operands()[0],
                              bitAnd->operands()[0]) ||
                    compare->operands()[1].kind() !=
                        MachineOperand::Kind::Immediate ||
                    compare->operands()[1].immediate() != 0)
                    continue;
                auto branch = std::next(compare);
                if (branch == instructions.end() ||
                    branch->opcode() != Opcode::Bcc ||
                    branch->operands().size() < 2 ||
                    branch->operands()[0].kind() !=
                        MachineOperand::Kind::ConditionCode ||
                    branch->operands()[1].kind() !=
                        MachineOperand::Kind::BasicBlock)
                    continue;
                CondCode condition =
                    branch->operands()[0].condition();
                if (condition != CondCode::EQ &&
                    condition != CondCode::NE)
                    continue;
                if (flagsUsedAfter(
                        owned.get(), std::next(branch)))
                    continue;

                MachineOperand source;
                MachineOperand maskOperand;
                std::optional<MaterializedI32> constantMask;
                if (bitAnd->opcode() == Opcode::ANDWri) {
                    if (!bitAnd->operands()[1]
                             .isVirtualRegister() ||
                        bitAnd->operands()[2].kind() !=
                            MachineOperand::Kind::Immediate)
                        continue;
                    source = bitAnd->operands()[1];
                    constantMask = MaterializedI32{
                        nullptr, 0,
                        static_cast<std::uint32_t>(
                            bitAnd->operands()[2].immediate())};
                } else {
                    auto rhsConstant =
                        materializedI32(bitAnd->operands()[2]);
                    auto lhsConstant =
                        materializedI32(bitAnd->operands()[1]);
                    if (rhsConstant) {
                        source = bitAnd->operands()[1];
                        maskOperand = bitAnd->operands()[2];
                        constantMask = rhsConstant;
                    } else if (lhsConstant) {
                        source = bitAnd->operands()[2];
                        maskOperand = bitAnd->operands()[1];
                        constantMask = lhsConstant;
                    } else {
                        source = bitAnd->operands()[1];
                        maskOperand = bitAnd->operands()[2];
                    }
                }
                if (!source.isVirtualRegister())
                    continue;

                bool oneBitMask =
                    constantMask &&
                    constantMask->value != 0 &&
                    (constantMask->value &
                     (constantMask->value - 1)) == 0;
                std::int64_t maskImmediate =
                    constantMask
                        ? static_cast<std::int64_t>(
                              constantMask->value)
                        : 0;
                bool immediateTest =
                    constantMask && !oneBitMask &&
                    InstrInfo::acceptsImmediate(
                        Opcode::TSTWri, maskImmediate);

                if (oneBitMask) {
                    unsigned bit = 0;
                    while (((constantMask->value >> bit) & 1U) ==
                           0)
                        ++bit;
                    MachineBasicBlock *target =
                        branch->operands()[1].basicBlock();
                    branch->setOpcode(condition == CondCode::EQ
                                          ? Opcode::TBZ
                                          : Opcode::TBNZ);
                    branch->operands().clear();
                    branch->addOperand(source)
                        .addOperand(
                            MachineOperand::immediate(bit))
                        .addOperand(
                            MachineOperand::block(target));
                    instructions.erase(compare);
                } else {
                    MachineInstr test(
                        immediateTest ? Opcode::TSTWri
                                      : Opcode::TSTWrr);
                    test.addOperand(source);
                    if (immediateTest) {
                        test.addOperand(
                            MachineOperand::immediate(
                                maskImmediate));
                    } else {
                        if (!maskOperand.isVirtualRegister())
                            continue;
                        test.addOperand(maskOperand);
                    }
                    test.addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, true,
                        true));
                    *compare = std::move(test);
                }
                instructions.erase(bitAnd);
                if (constantMask &&
                    constantMask->instruction &&
                    useCount[constantMask->reg] == 1 &&
                    (oneBitMask || immediateTest)) {
                    eraseInstruction(
                        constantMask->instruction);
                    function.registerInfo().eraseVirtualRegister(
                        constantMask->reg);
                }
                function.registerInfo().eraseVirtualRegister(
                    extracted);
                localChange = true;
                changed = true;
                break;
            }
        }
    }

    // A standalone equality comparison against zero is redundant when the
    // branch is its only flag consumer.  CBZ/CBNZ performs the same test
    // without creating an NZCV dependency.
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        for (auto compare = instructions.begin();
             compare != instructions.end();) {
            if (compare->opcode() != Opcode::CMPWri ||
                compare->operands().size() < 2 ||
                !compare->operands()[0].isVirtualRegister() ||
                compare->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                compare->operands()[1].immediate() != 0) {
                ++compare;
                continue;
            }
            auto branch = std::next(compare);
            if (branch == instructions.end() ||
                branch->opcode() != Opcode::Bcc ||
                branch->operands().size() < 2 ||
                branch->operands()[0].kind() !=
                    MachineOperand::Kind::ConditionCode ||
                branch->operands()[1].kind() !=
                    MachineOperand::Kind::BasicBlock) {
                ++compare;
                continue;
            }
            CondCode condition =
                branch->operands()[0].condition();
            if ((condition != CondCode::EQ &&
                 condition != CondCode::NE) ||
                flagsUsedAfter(owned.get(),
                               std::next(branch))) {
                ++compare;
                continue;
            }

            MachineOperand tested = compare->operands()[0];
            MachineBasicBlock *target =
                branch->operands()[1].basicBlock();
            branch->setOpcode(condition == CondCode::EQ
                                  ? Opcode::CBZ
                                  : Opcode::CBNZ);
            branch->operands().clear();
            branch->addOperand(std::move(tested))
                .addOperand(MachineOperand::block(target));
            compare = instructions.erase(compare);
            ++compare;
            changed = true;
        }
    }

    // Batch an exact-halving loop.  The matched even edge contains only
    // `state >>= 1`; the shared latch contains only `count += 1` and a test
    // against an odd sentinel.  CTZ therefore gives exactly the number of
    // consecutive iterations that can be collapsed.  The latch is cloned
    // onto the even edge so the odd predecessor keeps its unit increment.
    bool batched = true;
    while (batched) {
        batched = false;
        for (auto &parityOwned : function.blocks()) {
            MachineBasicBlock *parity = parityOwned.get();
            auto &parityInstructions = parity->instructions();
            if (parityInstructions.size() != 2)
                continue;
            auto bitBranch = parityInstructions.begin();
            auto parityFallthrough = std::next(bitBranch);
            if (bitBranch->opcode() != Opcode::TBZ ||
                bitBranch->operands().size() != 3 ||
                !bitBranch->operands()[0].isVirtualRegister() ||
                bitBranch->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                bitBranch->operands()[1].immediate() != 0 ||
                bitBranch->operands()[2].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                parityFallthrough->opcode() != Opcode::B)
                continue;

            MachineOperand state = bitBranch->operands()[0];
            MachineBasicBlock *even =
                bitBranch->operands()[2].basicBlock();
            if (!even || (even->instructions().size() != 3 &&
                          even->instructions().size() != 5))
                continue;
            bool incrementOnEdges = even->instructions().size() == 5;
            auto shift = even->instructions().begin();
            auto increment = shift;
            auto countCopy = shift;
            auto stateCopy = std::next(shift);
            if (incrementOnEdges) {
                increment = std::next(shift);
                countCopy = std::next(increment);
                stateCopy = std::next(countCopy);
            }
            auto toLatch = std::next(stateCopy);
            if (shift->opcode() != Opcode::ASRWri ||
                shift->operands().size() != 3 ||
                !shift->operands()[0].isVirtualRegister() ||
                !sameVReg(shift->operands()[1], state) ||
                shift->operands()[2].kind() !=
                    MachineOperand::Kind::Immediate ||
                shift->operands()[2].immediate() != 1 ||
                stateCopy->opcode() != Opcode::COPY ||
                stateCopy->operands().size() != 2 ||
                !stateCopy->operands()[0].isVirtualRegister() ||
                !sameVReg(stateCopy->operands()[1],
                          shift->operands()[0]) ||
                toLatch->opcode() != Opcode::B ||
                toLatch->operands().size() != 1 ||
                toLatch->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock)
                continue;

            if (incrementOnEdges &&
                (increment->opcode() != Opcode::ADDWri ||
                 increment->operands().size() != 3 ||
                 !increment->operands()[0].isVirtualRegister() ||
                 !increment->operands()[1].isVirtualRegister() ||
                 increment->operands()[2].kind() !=
                     MachineOperand::Kind::Immediate ||
                 increment->operands()[2].immediate() != 1 ||
                 countCopy->opcode() != Opcode::COPY ||
                 countCopy->operands().size() != 2 ||
                 !sameVReg(countCopy->operands()[1],
                           increment->operands()[0])))
                continue;

            MachineBasicBlock *latch =
                toLatch->operands()[0].basicBlock();
            const std::size_t expectedLatchSize =
                incrementOnEdges ? 3 : 4;
            if (!latch || latch->instructions().size() != expectedLatchSize)
                continue;
            auto compare = latch->instructions().begin();
            if (!incrementOnEdges) {
                increment = compare;
                compare = std::next(increment);
            }
            auto exitBranch = std::next(compare);
            auto continueBranch = std::next(exitBranch);
            if (increment->opcode() != Opcode::ADDWri ||
                increment->operands().size() != 3 ||
                !increment->operands()[0].isVirtualRegister() ||
                !increment->operands()[1].isVirtualRegister() ||
                increment->operands()[2].kind() !=
                    MachineOperand::Kind::Immediate ||
                increment->operands()[2].immediate() != 1 ||
                compare->opcode() != Opcode::CMPWri ||
                compare->operands().size() < 2 ||
                !sameVReg(compare->operands()[0],
                          stateCopy->operands()[0]) ||
                compare->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                (compare->operands()[1].immediate() & 1) == 0 ||
                exitBranch->opcode() != Opcode::Bcc ||
                exitBranch->operands().size() < 2 ||
                exitBranch->operands()[0].kind() !=
                    MachineOperand::Kind::ConditionCode ||
                exitBranch->operands()[0].condition() !=
                    CondCode::EQ ||
                exitBranch->operands()[1].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                continueBranch->opcode() != Opcode::B ||
                continueBranch->operands().size() != 1 ||
                continueBranch->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock)
                continue;

            MachineBasicBlock *exit =
                exitBranch->operands()[1].basicBlock();
            MachineBasicBlock *continuation =
                continueBranch->operands()[0].basicBlock();
            if (!exit || !continuation ||
                continuation->instructions().size() != 3)
                continue;
            bool copiesCount = false;
            bool copiesState = false;
            MachineInstr *continuationCountCopy = nullptr;
            const MachineOperand &edgeCount =
                incrementOnEdges ? countCopy->operands()[0]
                                 : increment->operands()[0];
            auto continuationIt =
                continuation->instructions().begin();
            for (unsigned i = 0; i < 2;
                 ++i, ++continuationIt) {
                if (continuationIt->opcode() != Opcode::COPY ||
                    continuationIt->operands().size() != 2)
                    break;
                bool isCountCopy =
                    sameVReg(continuationIt->operands()[0],
                             increment->operands()[1]) &&
                    sameVReg(continuationIt->operands()[1],
                             edgeCount);
                copiesCount |= isCountCopy;
                if (isCountCopy)
                    continuationCountCopy = &*continuationIt;
                copiesState |=
                    sameVReg(continuationIt->operands()[0],
                             state) &&
                    sameVReg(continuationIt->operands()[1],
                             stateCopy->operands()[0]);
            }
            if (!copiesCount || !copiesState ||
                continuationIt ==
                    continuation->instructions().end() ||
                continuationIt->opcode() != Opcode::B ||
                continuationIt->operands().size() != 1 ||
                continuationIt->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                continuationIt->operands()[0].basicBlock() !=
                    parity)
                continue;

            MachineInstr *exitCountCopy = nullptr;
            unsigned edgeCountUses = 0;
            for (auto &candidateBlock : function.blocks()) {
                for (MachineInstr &candidate :
                     candidateBlock->instructions()) {
                    for (unsigned operandIndex = 0;
                         operandIndex < candidate.operands().size();
                         ++operandIndex) {
                        MachineOperand &operand =
                            candidate.operands()[operandIndex];
                        if (operand.isDef || !sameVReg(operand, edgeCount))
                            continue;
                        ++edgeCountUses;
                        if (candidateBlock.get() == exit &&
                            candidate.opcode() == Opcode::COPY &&
                            operandIndex == 1)
                            exitCountCopy = &candidate;
                    }
                }
            }
            bool canReuseLoopCount =
                incrementOnEdges && continuationCountCopy &&
                exitCountCopy && edgeCountUses == 2;

            MachineBasicBlock *oddContinuation = nullptr;
            if (parityFallthrough->operands().size() == 1 &&
                parityFallthrough->operands()[0].kind() ==
                    MachineOperand::Kind::BasicBlock) {
                MachineBasicBlock *oddCompute =
                    parityFallthrough->operands()[0].basicBlock();
                for (MachineBasicBlock *predecessor :
                     latch->predecessors()) {
                    if (predecessor == even ||
                        (predecessor->instructions().size() != 3 &&
                         predecessor->instructions().size() != 5))
                        continue;
                    bool oddIncrementOnEdge =
                        predecessor->instructions().size() == 5;
                    if (oddIncrementOnEdge != incrementOnEdges)
                        continue;
                    auto oddIncrement =
                        predecessor->instructions().begin();
                    auto update = oddIncrementOnEdge
                                      ? std::next(oddIncrement)
                                      : oddIncrement;
                    auto oddCountCopy = std::next(update);
                    auto copy = oddIncrementOnEdge
                                    ? std::next(oddCountCopy)
                                    : std::next(update);
                    auto branch = std::next(copy);
                    if (oddIncrementOnEdge &&
                        (oddIncrement->opcode() != Opcode::ADDWri ||
                         oddIncrement->operands().size() != 3 ||
                         !sameVReg(oddIncrement->operands()[1],
                                   increment->operands()[1]) ||
                         oddIncrement->operands()[2].kind() !=
                             MachineOperand::Kind::Immediate ||
                         oddIncrement->operands()[2].immediate() != 1 ||
                         oddCountCopy->opcode() != Opcode::COPY ||
                         oddCountCopy->operands().size() != 2 ||
                         !sameVReg(oddCountCopy->operands()[0],
                                   countCopy->operands()[0]) ||
                         !sameVReg(oddCountCopy->operands()[1],
                                   oddIncrement->operands()[0])))
                        continue;
                    if (update->opcode() != Opcode::ADDWri ||
                        update->operands().size() != 3 ||
                        update->operands()[2].kind() !=
                            MachineOperand::Kind::Immediate ||
                        update->operands()[2].immediate() != 1 ||
                        copy->opcode() != Opcode::COPY ||
                        copy->operands().size() != 2 ||
                        !sameVReg(copy->operands()[0],
                                  stateCopy->operands()[0]) ||
                        !sameVReg(copy->operands()[1],
                                  update->operands()[0]) ||
                        branch->opcode() != Opcode::B ||
                        branch->operands().size() != 1 ||
                        branch->operands()[0].kind() !=
                            MachineOperand::Kind::BasicBlock ||
                        branch->operands()[0].basicBlock() !=
                            latch ||
                        predecessor->predecessors().size() != 1 ||
                        predecessor->predecessors()[0] !=
                            oddCompute)
                        continue;

                    bool computesThreeTimesOdd = false;
                    for (const MachineInstr &instruction :
                         oddCompute->instructions())
                        if (instruction.opcode() ==
                                Opcode::ADDWlsl &&
                            instruction.operands().size() == 4 &&
                            sameVReg(
                                instruction.operands()[0],
                                update->operands()[1]) &&
                            sameVReg(
                                instruction.operands()[1],
                                state) &&
                            sameVReg(
                                instruction.operands()[2],
                                state) &&
                            instruction.operands()[3].kind() ==
                                MachineOperand::Kind::Immediate &&
                            instruction.operands()[3].immediate() ==
                                1)
                            computesThreeTimesOdd = true;
                    if (computesThreeTimesOdd) {
                        oddContinuation = predecessor;
                        break;
                    }
                }
            }

            VReg reversed = function.registerInfo()
                .createVirtualRegister(RegClass::GPR32,
                                       ValueType::I32);
            MachineInstr reverse(Opcode::RBITW);
            reverse
                .addOperand(MachineOperand::vreg(
                    reversed, RegClass::GPR32, true))
                .addOperand(state);
            auto reversedDefinition =
                even->instructions().insert(shift,
                                             std::move(reverse));
            function.registerInfo().setDefinition(
                reversed, &*reversedDefinition);

            VReg shiftAmount = function.registerInfo()
                .createVirtualRegister(RegClass::GPR32,
                                       ValueType::I32);
            MachineInstr countZeros(Opcode::CLZW);
            countZeros
                .addOperand(MachineOperand::vreg(
                    shiftAmount, RegClass::GPR32, true))
                .addOperand(MachineOperand::vreg(
                    reversed, RegClass::GPR32));
            auto countDefinition =
                even->instructions().insert(
                    shift, std::move(countZeros));
            function.registerInfo().setDefinition(
                shiftAmount, &*countDefinition);

            shift->setOpcode(Opcode::ASRWrr);
            shift->operands()[2] = MachineOperand::vreg(
                shiftAmount, RegClass::GPR32);
            even->instructions().erase(toLatch);

            MachineInstr batchedIncrement(Opcode::ADDWrr);
            batchedIncrement
                .addOperand(canReuseLoopCount
                                ? MachineOperand::vreg(
                                      increment->operands()[1]
                                          .virtualRegister(),
                                      increment->operands()[1]
                                          .regClass(),
                                      true)
                                : increment->operands()[0])
                .addOperand(increment->operands()[1])
                .addOperand(MachineOperand::vreg(
                    shiftAmount, RegClass::GPR32));
            if (incrementOnEdges)
                *increment = std::move(batchedIncrement);
            else
                even->append(std::move(batchedIncrement));
            even->append(*compare);
            even->append(*exitBranch);
            even->append(*continueBranch);

            even->removeSuccessor(latch);
            even->addSuccessor(exit);
            even->addSuccessor(continuation);

            // The fallthrough of TBZ proves the state is odd.  If an edge
            // computes `3*state + 1`, modular low-bit arithmetic proves the
            // result is even, so it may enter the same batched halving
            // sequence directly.  Clone with fresh virtual temporaries;
            // no physical scratch or input-dependent speculation is used.
            if (oddContinuation) {
                bool oddIncrementOnEdge =
                    oddContinuation->instructions().size() == 5;
                auto oddIncrement =
                    oddContinuation->instructions().begin();
                auto update = oddIncrementOnEdge
                                  ? std::next(oddIncrement)
                                  : oddIncrement;
                auto oddCountCopy = std::next(update);
                auto copy = oddIncrementOnEdge
                                ? std::next(oddCountCopy)
                                : std::next(update);
                auto oldBranch = std::next(copy);
                auto oddBatchInsert = oddIncrementOnEdge
                                          ? oddCountCopy
                                          : copy;
                MachineOperand updatedState =
                    MachineOperand::vreg(
                        update->operands()[0]
                            .virtualRegister(),
                        update->operands()[0].regClass());

                VReg oddReversed = function.registerInfo()
                    .createVirtualRegister(RegClass::GPR32,
                                           ValueType::I32);
                MachineInstr reverseOdd(Opcode::RBITW);
                reverseOdd
                    .addOperand(MachineOperand::vreg(
                        oddReversed, RegClass::GPR32, true))
                    .addOperand(updatedState);
                auto reverseOddDefinition =
                    oddContinuation->instructions().insert(
                        oddBatchInsert, std::move(reverseOdd));
                function.registerInfo().setDefinition(
                    oddReversed, &*reverseOddDefinition);

                VReg oddShiftAmount = function.registerInfo()
                    .createVirtualRegister(RegClass::GPR32,
                                           ValueType::I32);
                MachineInstr countOddZeros(Opcode::CLZW);
                countOddZeros
                    .addOperand(MachineOperand::vreg(
                        oddShiftAmount, RegClass::GPR32, true))
                    .addOperand(MachineOperand::vreg(
                        oddReversed, RegClass::GPR32));
                auto oddCountDefinition =
                    oddContinuation->instructions().insert(
                        oddBatchInsert, std::move(countOddZeros));
                function.registerInfo().setDefinition(
                    oddShiftAmount, &*oddCountDefinition);

                MachineOperand shiftedState =
                    copy->operands()[0];
                copy->setOpcode(Opcode::ASRWrr);
                copy->operands().clear();
                copy->addOperand(shiftedState)
                    .addOperand(updatedState)
                    .addOperand(MachineOperand::vreg(
                        oddShiftAmount, RegClass::GPR32));
                oddContinuation->instructions().erase(oldBranch);

                if (oddIncrementOnEdge) {
                    MachineOperand countDestination = canReuseLoopCount
                        ? MachineOperand::vreg(
                              increment->operands()[1]
                                  .virtualRegister(),
                              increment->operands()[1].regClass(),
                              true)
                        : oddCountCopy->operands()[0];
                    oddCountCopy->setOpcode(Opcode::ADDWrr);
                    oddCountCopy->operands().clear();
                    oddCountCopy
                        ->addOperand(countDestination)
                        .addOperand(oddIncrement->operands()[0])
                        .addOperand(MachineOperand::vreg(
                            oddShiftAmount, RegClass::GPR32));
                } else {
                    VReg batchedCount = function.registerInfo()
                        .createVirtualRegister(RegClass::GPR32,
                                               ValueType::I32);
                    MachineInstr addShifts(Opcode::ADDWrr);
                    addShifts
                        .addOperand(MachineOperand::vreg(
                            batchedCount, RegClass::GPR32, true))
                        .addOperand(increment->operands()[1])
                        .addOperand(MachineOperand::vreg(
                            oddShiftAmount, RegClass::GPR32));
                    MachineInstr &addShiftsDefinition =
                        oddContinuation->append(
                            std::move(addShifts));
                    function.registerInfo().setDefinition(
                        batchedCount, &addShiftsDefinition);

                    MachineInstr addOddIteration(Opcode::ADDWri);
                    addOddIteration
                        .addOperand(increment->operands()[0])
                        .addOperand(MachineOperand::vreg(
                            batchedCount, RegClass::GPR32))
                        .addOperand(MachineOperand::immediate(1));
                    oddContinuation->append(
                        std::move(addOddIteration));
                }
                oddContinuation->append(*compare);
                oddContinuation->append(*exitBranch);
                oddContinuation->append(*continueBranch);
                oddContinuation->removeSuccessor(latch);
                oddContinuation->addSuccessor(exit);
                oddContinuation->addSuccessor(continuation);
            }
            if (canReuseLoopCount) {
                exitCountCopy->operands()[1] = MachineOperand::vreg(
                    increment->operands()[1].virtualRegister(),
                    increment->operands()[1].regClass());
                even->instructions().erase(countCopy);
                for (auto copyIt = continuation->instructions().begin();
                     copyIt != continuation->instructions().end();
                     ++copyIt) {
                    if (&*copyIt != continuationCountCopy)
                        continue;
                    continuation->instructions().erase(copyIt);
                    break;
                }
            }
            function.clearProperty(
                MachineProperty::TracksLiveness);
            changed = true;
            batched = true;
            break;
        }
    }
    if (changed)
        function.clearProperty(
            MachineProperty::TracksLiveness);
    return changed;
}

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
                sameRegister(instruction->operands()[0],
                             instruction->operands()[1])) {
                instruction = instructions.erase(instruction);
                changed = true;
                continue;
            }
            ++instruction;
        }
    }

    using PhysSet = std::unordered_set<PhysReg>;
    std::unordered_map<MachineBasicBlock *, PhysSet> physicalUses;
    std::unordered_map<MachineBasicBlock *, PhysSet> physicalDefs;
    std::unordered_map<MachineBasicBlock *, PhysSet> physicalLiveIn;
    std::unordered_map<MachineBasicBlock *, PhysSet> physicalLiveOut;
    auto addUse = [&](MachineBasicBlock *block, PhysReg reg) {
        if (!physicalDefs[block].count(reg))
            physicalUses[block].insert(reg);
    };
    for (auto &owned : function.blocks()) {
        MachineBasicBlock *block = owned.get();
        for (const MachineInstr &instruction :
             block->instructions()) {
            for (const MachineOperand &operand :
                 instruction.operands()) {
                if (operand.isPhysicalRegister() &&
                    !operand.isDef)
                    addUse(block, operand.physicalRegister());
            }
            if (instruction.isCall()) {
                for (unsigned index = 0; index < 8; ++index) {
                    addUse(block, integerArgumentRegister(index));
                    addUse(block, vectorArgumentRegister(index));
                }
            } else if (instruction.opcode() == Opcode::RET) {
                addUse(block, PhysReg::X0);
                addUse(block, PhysReg::V0);
                addUse(block, PhysReg::X30);
            }
            // Uses are collected before definitions so an allocated
            // read/modify/write such as `add x9, x9, #1` contributes x9 to
            // the block's live-in set when it is the first mention.
            for (const MachineOperand &operand :
                 instruction.operands())
                if (operand.isPhysicalRegister() &&
                    operand.isDef)
                    physicalDefs[block].insert(
                        operand.physicalRegister());
            if (instruction.isCall()) {
                for (unsigned raw =
                         static_cast<unsigned>(PhysReg::X0);
                     raw <= static_cast<unsigned>(PhysReg::V31);
                     ++raw) {
                    PhysReg reg = static_cast<PhysReg>(raw);
                    if (RegisterInfo::isCallerSaved(reg))
                        physicalDefs[block].insert(reg);
                }
            }
        }
    }
    bool livenessChanged = true;
    while (livenessChanged) {
        livenessChanged = false;
        for (auto block = function.blocks().rbegin();
             block != function.blocks().rend(); ++block) {
            MachineBasicBlock *current = block->get();
            PhysSet nextOut;
            for (MachineBasicBlock *successor :
                 current->successors())
                nextOut.insert(
                    physicalLiveIn[successor].begin(),
                    physicalLiveIn[successor].end());
            PhysSet nextIn = physicalUses[current];
            for (PhysReg reg : nextOut)
                if (!physicalDefs[current].count(reg))
                    nextIn.insert(reg);
            if (nextOut != physicalLiveOut[current] ||
                nextIn != physicalLiveIn[current]) {
                physicalLiveOut[current] = std::move(nextOut);
                physicalLiveIn[current] = std::move(nextIn);
                livenessChanged = true;
            }
        }
    }

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
                    if (isCallArgumentRegister(destination)) {
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
                    } else if (!physicalLiveOut[block.get()].count(
                            destination) &&
                        (scan->opcode() != Opcode::RET ||
                         (!isReturnRegister(destination) &&
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
            if (!vectorInsert && !vectorAccumulate)
                continue;
            if (insert->operands().size() != 4 ||
                !insert->operands()[0].isPhysicalRegister() ||
                !insert->operands()[0].isDef ||
                !insert->operands()[1].isPhysicalRegister())
                throw std::logic_error(
                    "malformed vector insert after register allocation");
            MachineOperand &destination = insert->operands()[0];
            MachineOperand &source = insert->operands()[1];
            if (!sameRegister(destination, source)) {
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

// Expand one MOVi32/MOVi64 into MOVZ + MOVK pieces.  Matches the historical
// asm-printer encoding: first non-zero 16-bit slice becomes MOVZ, remaining
// non-zero slices become MOVK.  An all-zero immediate becomes `movz #0`.
void expandIntegerImmediate(MachineBasicBlock::InstrList &instructions,
                            MachineBasicBlock::InstrList::iterator materialize) {
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

    unsigned first = pieces;
    for (unsigned i = 0; i < pieces; ++i)
        if (((value >> (i * 16)) & 0xffffU) != 0) {
            first = i;
            break;
        }

    auto emitMovz = [&](unsigned slice, std::uint64_t imm) {
        MachineInstr movz(Opcode::MOVZ);
        movz.addOperand(MachineOperand::physReg(reg, regClass, true));
        movz.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(imm)));
        movz.addOperand(MachineOperand::immediate(static_cast<std::int64_t>(slice * 16)));
        instructions.insert(materialize, std::move(movz));
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

    if (first == pieces) {
        emitMovz(0, 0);
    } else {
        emitMovz(first, (value >> (first * 16)) & 0xffffU);
        for (unsigned i = 0; i < pieces; ++i) {
            if (i == first)
                continue;
            const std::uint64_t piece = (value >> (i * 16)) & 0xffffU;
            if (!piece)
                continue;
            emitMovk(i, piece);
        }
    }
    instructions.erase(materialize);
}

} // namespace

bool PostRAInstructionExpansion::expandConstantMaterializations(
    MachineFunction &function) const {
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
            expandIntegerImmediate(instructions, current);
            changed = true;
        }
    }
    return changed;
}

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

    std::unordered_map<VReg, unsigned> useCount;
    for (const auto &block : function.blocks())
        for (const MachineInstr &instruction : block->instructions())
            for (const MachineOperand &operand : instruction.operands())
                if (operand.isVirtualRegister() && !operand.isDef)
                    ++useCount[operand.virtualRegister()];

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

        MachineInstr *definition =
            function.registerInfo().get(reg).definition;
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
                    useCount[reg] != 1) {
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
                    useCount[reg] == 1) {
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

bool MachineBlockPlacement::run(MachineFunction &function) const {
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
        for (auto blockIt = std::next(blocks.begin());
             blockIt != blocks.end(); ++blockIt) {
            MachineBasicBlock *forwarder = blockIt->get();
            if (forwarder->instructions().size() != 1)
                continue;
            const MachineInstr &branch =
                forwarder->instructions().front();
            if (branch.opcode() != Opcode::B ||
                branch.operands().size() != 1 ||
                branch.operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock)
                continue;
            MachineBasicBlock *target =
                branch.operands()[0].basicBlock();
            if (!target || target == forwarder)
                continue;

            std::vector<MachineBasicBlock *> predecessors =
                forwarder->predecessors();
            for (MachineBasicBlock *predecessor : predecessors) {
                for (MachineInstr &instruction :
                     predecessor->instructions())
                    for (MachineOperand &operand :
                         instruction.operands())
                        if (operand.kind() ==
                                MachineOperand::Kind::BasicBlock &&
                            operand.basicBlock() == forwarder)
                            operand =
                                MachineOperand::block(target);
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
    auto extendChain = [&](MachineBasicBlock *start) {
        MachineBasicBlock *current = start;
        while (current && placed.insert(current).second) {
            order.push_back(current);
            MachineBasicBlock *preferredFallthrough = nullptr;
            MachineBasicBlock *likelySuccessor = nullptr;
            unsigned deepestSuccessor = 0;
            bool depthsDiffer = false;
            if (!current->successors().empty()) {
                deepestSuccessor =
                    current->successors().front()->loopDepth;
                unsigned shallowestSuccessor = deepestSuccessor;
                for (MachineBasicBlock *successor :
                     current->successors()) {
                    deepestSuccessor =
                        std::max(deepestSuccessor,
                                 successor->loopDepth);
                    shallowestSuccessor =
                        std::min(shallowestSuccessor,
                                 successor->loopDepth);
                }
                depthsDiffer =
                    deepestSuccessor != shallowestSuccessor;
            }

            // In the absence of profile data, natural-loop membership is
            // the strongest static frequency signal.  Record the explicit
            // fallthrough as the final tie breaker, but do not let it hide a
            // successor that immediately enters a deeper forward region.
            if (!depthsDiffer &&
                current->instructions().size() >= 2) {
                auto unconditional =
                    std::prev(current->instructions().end());
                auto conditional = std::prev(unconditional);
                bool hasConditional =
                    conditional->opcode() == Opcode::Bcc ||
                    conditional->opcode() == Opcode::CBZ ||
                    conditional->opcode() == Opcode::CBNZ;
                if (unconditional->opcode() == Opcode::B &&
                    hasConditional &&
                    unconditional->operands().size() == 1 &&
                    unconditional->operands()[0].kind() ==
                        MachineOperand::Kind::BasicBlock) {
                    preferredFallthrough =
                        unconditional->operands()[0].basicBlock();
                    MachineBasicBlock *conditionalTarget = nullptr;
                    // Equality with zero or another single value is usually
                    // the less frequent edge.  Use the same static direction
                    // for flag branches and their CBZ/CBNZ counterparts.
                    if (conditional->opcode() == Opcode::Bcc &&
                        conditional->operands().size() >= 2 &&
                        conditional->operands()[0].kind() ==
                            MachineOperand::Kind::ConditionCode &&
                        conditional->operands()[1].kind() ==
                            MachineOperand::Kind::BasicBlock) {
                        conditionalTarget =
                            conditional->operands()[1].basicBlock();
                        CondCode condition =
                            conditional->operands()[0].condition();
                        if (condition == CondCode::EQ)
                            likelySuccessor = preferredFallthrough;
                        else if (condition == CondCode::NE)
                            likelySuccessor = conditionalTarget;
                    } else if (
                        (conditional->opcode() == Opcode::CBZ ||
                         conditional->opcode() == Opcode::CBNZ) &&
                        conditional->operands().size() >= 2 &&
                        conditional->operands()[1].kind() ==
                            MachineOperand::Kind::BasicBlock) {
                        conditionalTarget =
                            conditional->operands()[1].basicBlock();
                        likelySuccessor =
                            conditional->opcode() == Opcode::CBZ
                                ? preferredFallthrough
                                : conditionalTarget;
                    }
                }
            }

            struct ForwardTraceScore {
                unsigned deepestLoop = 0;
                unsigned weightedLength = 0;
            };
            auto scoreForwardTrace =
                [&](MachineBasicBlock *traceStart) {
                    constexpr std::size_t traceBlockLimit = 64;
                    std::unordered_map<MachineBasicBlock *,
                                       ForwardTraceScore> memo;
                    std::unordered_set<MachineBasicBlock *> active;
                    auto visit = [&](auto &&self,
                                     MachineBasicBlock *block)
                        -> ForwardTraceScore {
                        if (!block || block == current)
                            return {};
                        if (block != traceStart &&
                            (placed.count(block) ||
                             block->number() <= current->number()))
                            return {};
                        if (auto found = memo.find(block);
                            found != memo.end())
                            return found->second;
                        if (!active.insert(block).second)
                            return {};
                        if (memo.size() + active.size() >
                            traceBlockLimit) {
                            active.erase(block);
                            return ForwardTraceScore{
                                block->loopDepth,
                                1 + 4 * std::min(
                                    block->loopDepth, 4U)};
                        }

                        ForwardTraceScore bestChild;
                        for (MachineBasicBlock *successor :
                             block->successors()) {
                            ForwardTraceScore child =
                                self(self, successor);
                            if (child.deepestLoop >
                                    bestChild.deepestLoop ||
                                (child.deepestLoop ==
                                     bestChild.deepestLoop &&
                                 child.weightedLength >
                                     bestChild.weightedLength))
                                bestChild = child;
                        }
                        active.erase(block);

                        ForwardTraceScore result;
                        result.deepestLoop =
                            std::max(block->loopDepth,
                                     bestChild.deepestLoop);
                        result.weightedLength =
                            1 + 4 * std::min(block->loopDepth, 4U) +
                            bestChild.weightedLength;
                        memo.emplace(block, result);
                        return result;
                    };
                    return visit(visit, traceStart);
                };

            MachineBasicBlock *best = nullptr;
            int bestScore = -1;
            for (unsigned i = 0;
                 i < current->successors().size(); ++i) {
                MachineBasicBlock *successor =
                    current->successors()[i];
                if (placed.count(successor))
                    continue;
                int score =
                    successor->predecessors().size() == 1 ? 100 : 0;
                if (depthsDiffer &&
                    successor->loopDepth == deepestSuccessor)
                    score += 300;
                else if (!depthsDiffer) {
                    ForwardTraceScore trace =
                        scoreForwardTrace(successor);
                    score += static_cast<int>(
                        std::min(trace.deepestLoop, 8U) * 100000U);
                    if (successor == likelySuccessor)
                        score += 10000;
                    score += static_cast<int>(
                        std::min(trace.weightedLength, 999U) * 10U);
                    if (successor == preferredFallthrough)
                        ++score;
                }
                else if (!preferredFallthrough && i == 0)
                    score += 10;
                score += successor->number() > current->number()
                             ? 1 : 0;
                if (score > bestScore) {
                    bestScore = score;
                    best = successor;
                }
            }
            current = best;
        }
    };
    extendChain(blocks.front().get());
    for (const auto &block : blocks)
        if (!placed.count(block.get()))
            extendChain(block.get());

    // Rotate a conditional loop latch immediately before its header when
    // every displaced CFG edge is explicit.  The final branch cleanup can
    // then invert the exit condition and make the hot backedge fall through,
    // matching LLVM's MachineBlockPlacement loop rotation.  Block numbers
    // are used only as the construction-order indication of a backedge; no
    // block name or source-level pattern participates in the decision.
    for (const auto &owned : blocks) {
        MachineBasicBlock *latch = owned.get();
        if (latch == function.entryBlock() ||
            latch->successors().size() != 2 ||
            latch->loopDepth == 0)
            continue;
        MachineBasicBlock *header = nullptr;
        for (MachineBasicBlock *successor :
             latch->successors())
            if (successor->loopDepth > 0 &&
                successor->number() < latch->number()) {
                header = successor;
                break;
            }
        if (!header || header == function.entryBlock())
            continue;

        auto hasExplicitTransfer =
            [](MachineBasicBlock *block) {
                return !block->instructions().empty() &&
                       block->instructions().back().isTerminator();
            };
        bool safe = true;
        for (MachineBasicBlock *predecessor :
             header->predecessors())
            if (predecessor != latch &&
                !hasExplicitTransfer(predecessor))
                safe = false;
        for (MachineBasicBlock *predecessor :
             latch->predecessors())
            if (!hasExplicitTransfer(predecessor))
                safe = false;
        if (!safe)
            continue;

        auto latchPosition =
            std::find(order.begin(), order.end(), latch);
        auto headerPosition =
            std::find(order.begin(), order.end(), header);
        if (latchPosition == order.end() ||
            headerPosition == order.end() ||
            std::next(latchPosition) == headerPosition)
            continue;
        order.erase(latchPosition);
        headerPosition =
            std::find(order.begin(), order.end(), header);
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

    for (std::size_t i = 0; i + 1 < blocks.size(); ++i) {
        MachineBasicBlock *fallthrough = blocks[i + 1].get();
        auto &instructions = blocks[i]->instructions();
        if (instructions.empty())
            continue;
        auto unconditional = std::prev(instructions.end());
        if (unconditional->opcode() != Opcode::B ||
            unconditional->operands().empty())
            continue;
        MachineBasicBlock *fallback =
            unconditional->operands()[0].basicBlock();
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
            conditionalTarget =
                conditional->operands()[1].basicBlock();
        else if ((conditional->opcode() == Opcode::CBZ ||
                  conditional->opcode() == Opcode::CBNZ) &&
                 conditional->operands().size() >= 2)
            conditionalTarget =
                conditional->operands()[1].basicBlock();
        if (conditionalTarget != fallthrough)
            continue;

        if (conditional->opcode() == Opcode::Bcc) {
            CondCode inverse = inverseCondition(
                conditional->operands()[0].condition());
            conditional->operands()[0] =
                MachineOperand::condition(inverse);
            conditional->operands()[1] =
                MachineOperand::block(fallback);
        } else {
            conditional->setOpcode(
                conditional->opcode() == Opcode::CBZ
                    ? Opcode::CBNZ : Opcode::CBZ);
            conditional->operands()[1] =
                MachineOperand::block(fallback);
        }
        instructions.erase(unconditional);
        changed = true;
    }
    return changed;
}

} // namespace backend::aarch64
