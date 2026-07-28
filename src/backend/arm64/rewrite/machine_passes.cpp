#include "../../../include/backend/arm64/rewrite/machine_passes.hpp"

#include <deque>
#include <algorithm>
#include <cstdlib>
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
            bool constant =
                it->opcode() == Opcode::MOVi32 ||
                it->opcode() == Opcode::MOVi64 ||
                it->opcode() == Opcode::MOVIv4Zero;
            if (!constant || it->operands().empty() ||
                !it->operands()[0].isVirtualRegister() ||
                !it->operands()[0].isDef) {
                ++it;
                continue;
            }

            std::string key =
                std::to_string(static_cast<unsigned>(it->opcode()));
            if (it->opcode() != Opcode::MOVIv4Zero) {
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

    struct AddressForm {
        VReg root = 0;
        std::int64_t offset = 0;
        bool valid = false;
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

                bool load =
                    it->opcode() == Opcode::LDRQui;
                bool store =
                    it->opcode() == Opcode::STRQui;
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
                    MemoryCandidate{it, form});
            }

            auto tryPair = [&](std::vector<MemoryCandidate> &candidates,
                               bool load) {
                for (std::size_t i = 0;
                     i < candidates.size(); ++i) {
                    for (std::size_t j = i + 1;
                         j < candidates.size(); ++j) {
                        auto &lhs = candidates[i];
                        auto &rhs = candidates[j];
                        if (lhs.address.root !=
                                rhs.address.root ||
                            std::llabs(
                                lhs.address.offset -
                                rhs.address.offset) != 16)
                            continue;
                        std::int64_t lowerOffset =
                            std::min(lhs.address.offset,
                                     rhs.address.offset);
                        if (lowerOffset % 16 != 0 ||
                            lowerOffset / 16 < -64 ||
                            lowerOffset / 16 > 63)
                            continue;

                        bool memoryBarrier = false;
                        for (auto scan =
                                 std::next(lhs.instruction);
                             scan != rhs.instruction; ++scan)
                            if (scan->isCall() ||
                                scan->mayStore() ||
                                (!load &&
                                 scan->mayLoad())) {
                                memoryBarrier = true;
                                break;
                            }
                        if (memoryBarrier)
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
                            load ? Opcode::LDPQi
                                 : Opcode::STPQi);
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
                        pair.addMemoryOperand(
                            MachineMemOperand{
                                load
                                    ? MachineMemOperand::Access::
                                          Load
                                    : MachineMemOperand::Access::
                                          Store,
                                32, 16, nullptr,
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
        case Opcode::MOVIv4Zero:
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

            auto select = std::next(booleanCompare);
            while (select != instructions.end()) {
                bool candidate =
                    (select->opcode() == Opcode::CSELW ||
                     select->opcode() == Opcode::CSELX ||
                     select->opcode() == Opcode::FCSELS) &&
                    select->operands().size() >= 4 &&
                    select->operands()[3].kind() ==
                        MachineOperand::Kind::ConditionCode;
                if (candidate)
                    break;
                const InstrDesc &descriptor =
                    InstrInfo::get(select->opcode());
                if (descriptor.setsFlags || descriptor.usesFlags ||
                    select->isCall() || select->isTerminator())
                    break;
                ++select;
            }
            if (select == instructions.end() ||
                (select->opcode() != Opcode::CSELW &&
                 select->opcode() != Opcode::CSELX &&
                 select->opcode() != Opcode::FCSELS) ||
                select->operands().size() < 4) {
                ++set;
                continue;
            }

            CondCode booleanCondition =
                select->operands()[3].condition();
            if (booleanCondition != CondCode::NE &&
                booleanCondition != CondCode::EQ) {
                ++set;
                continue;
            }
            CondCode originalCondition =
                set->operands()[1].condition();
            select->operands()[3] =
                MachineOperand::condition(
                    booleanCondition == CondCode::NE
                        ? originalCondition
                        : inverseCondition(originalCondition));
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

    // Fold a selected low-bit extraction and its sole compare into the
    // architectural bit-test branch.  This is deliberately pre-RA: the
    // source remains a virtual register and no physical scratch is invented.
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        bool localChange = true;
        while (localChange) {
            localChange = false;
            for (auto bitAnd = instructions.begin();
                 bitAnd != instructions.end(); ++bitAnd) {
                if (bitAnd->opcode() != Opcode::ANDWrr ||
                    bitAnd->operands().size() != 3 ||
                    !bitAnd->operands()[0].isVirtualRegister())
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

                auto findOne = [&](const MachineOperand &candidate)
                    -> MachineInstr * {
                    if (!candidate.isVirtualRegister())
                        return nullptr;
                    const VRegInfo &info =
                        function.registerInfo().get(
                            candidate.virtualRegister());
                    MachineInstr *definition = info.definition;
                    if (definition &&
                        definition->opcode() == Opcode::MOVi32 &&
                        definition->operands().size() == 2 &&
                        sameVReg(definition->operands()[0],
                                 candidate) &&
                        definition->operands()[1].kind() ==
                            MachineOperand::Kind::Immediate &&
                        definition->operands()[1].immediate() == 1)
                        return definition;
                    return nullptr;
                };
                MachineInstr *constant =
                    findOne(bitAnd->operands()[2]);
                std::size_t sourceIndex = 1;
                if (!constant) {
                    constant = findOne(bitAnd->operands()[1]);
                    sourceIndex = 2;
                }
                if (!constant ||
                    !bitAnd->operands()[sourceIndex]
                         .isVirtualRegister())
                    continue;

                VReg mask =
                    constant->operands()[0].virtualRegister();
                VReg extracted =
                    bitAnd->operands()[0].virtualRegister();
                unsigned maskUses = 0;
                unsigned extractedUses = 0;
                for (const auto &block : function.blocks())
                    for (const MachineInstr &instruction :
                         block->instructions())
                        for (const MachineOperand &operand :
                             instruction.operands()) {
                            if (!operand.isVirtualRegister() ||
                                operand.isDef)
                                continue;
                            maskUses +=
                                operand.virtualRegister() == mask;
                            extractedUses +=
                                operand.virtualRegister() == extracted;
                        }
                if (maskUses != 1 || extractedUses != 1)
                    continue;

                MachineBasicBlock *target =
                    branch->operands()[1].basicBlock();
                MachineOperand source =
                    bitAnd->operands()[sourceIndex];
                branch->setOpcode(condition == CondCode::EQ
                                      ? Opcode::TBZ
                                      : Opcode::TBNZ);
                branch->operands().clear();
                branch->addOperand(std::move(source))
                    .addOperand(MachineOperand::immediate(0))
                    .addOperand(MachineOperand::block(target));
                instructions.erase(compare);
                instructions.erase(bitAnd);
                for (auto &definitionBlock : function.blocks()) {
                    auto &definitionInstructions =
                        definitionBlock->instructions();
                    auto definition = std::find_if(
                        definitionInstructions.begin(),
                        definitionInstructions.end(),
                        [&](const MachineInstr &instruction) {
                            return &instruction == constant;
                        });
                    if (definition !=
                        definitionInstructions.end()) {
                        definitionInstructions.erase(definition);
                        break;
                    }
                }
                function.registerInfo().eraseVirtualRegister(mask);
                function.registerInfo().eraseVirtualRegister(extracted);
                localChange = true;
                changed = true;
                break;
            }
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
            if (!even || even->instructions().size() != 3)
                continue;
            auto shift = even->instructions().begin();
            auto stateCopy = std::next(shift);
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

            MachineBasicBlock *latch =
                toLatch->operands()[0].basicBlock();
            if (!latch || latch->instructions().size() != 4)
                continue;
            auto increment = latch->instructions().begin();
            auto compare = std::next(increment);
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
            auto continuationIt =
                continuation->instructions().begin();
            for (unsigned i = 0; i < 2;
                 ++i, ++continuationIt) {
                if (continuationIt->opcode() != Opcode::COPY ||
                    continuationIt->operands().size() != 2)
                    break;
                copiesCount |=
                    sameVReg(continuationIt->operands()[0],
                             increment->operands()[1]) &&
                    sameVReg(continuationIt->operands()[1],
                             increment->operands()[0]);
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

            MachineBasicBlock *oddContinuation = nullptr;
            if (parityFallthrough->operands().size() == 1 &&
                parityFallthrough->operands()[0].kind() ==
                    MachineOperand::Kind::BasicBlock) {
                MachineBasicBlock *oddCompute =
                    parityFallthrough->operands()[0].basicBlock();
                for (MachineBasicBlock *predecessor :
                     latch->predecessors()) {
                    if (predecessor == even ||
                        predecessor->instructions().size() != 3)
                        continue;
                    auto update =
                        predecessor->instructions().begin();
                    auto copy = std::next(update);
                    auto branch = std::next(copy);
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
                .addOperand(increment->operands()[0])
                .addOperand(increment->operands()[1])
                .addOperand(MachineOperand::vreg(
                    shiftAmount, RegClass::GPR32));
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
                auto update =
                    oddContinuation->instructions().begin();
                auto copy = std::next(update);
                auto oldBranch = std::next(copy);
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
                        copy, std::move(reverseOdd));
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
                        copy, std::move(countOddZeros));
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
                oddContinuation->append(*compare);
                oddContinuation->append(*exitBranch);
                oddContinuation->append(*continueBranch);
                oddContinuation->removeSuccessor(latch);
                oddContinuation->addSuccessor(exit);
                oddContinuation->addSuccessor(continuation);
            }
            function.clearProperty(
                MachineProperty::TracksLiveness);
            changed = true;
            batched = true;
            break;
        }
    }
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
                    if (!physicalLiveOut[block.get()].count(
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
            if (insert->opcode() != Opcode::INSv4i32 &&
                insert->opcode() != Opcode::INSv4f32)
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

bool PostRAAddressingOptimizer::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        return false;
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

    bool changed = false;
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
            // the strongest static frequency signal.  When both arms have
            // the same loop depth, preserve the explicit conditional edge
            // and lay out the unconditional successor as fallthrough.  This
            // avoids arbitrarily inverting source CFG branches merely
            // because the conditional target has a single predecessor.
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
                        MachineOperand::Kind::BasicBlock)
                    preferredFallthrough =
                        unconditional->operands()[0].basicBlock();
            }

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
                else if (!depthsDiffer &&
                         successor == preferredFallthrough)
                    score += 200;
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
