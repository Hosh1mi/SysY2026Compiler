#include "../../include/backend/arm64/regalloc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

MachineOperand replacementRegister(const MachineOperand &old, VReg reg,
                                   RegClass regClass) {
    MachineOperand replacement =
        MachineOperand::vreg(reg, regClass, old.isDef);
    replacement.isImplicit = old.isImplicit;
    replacement.isKill = old.isKill;
    replacement.isDead = old.isDead;
    replacement.isUndef = old.isUndef;
    replacement.isEarlyClobber = old.isEarlyClobber;
    replacement.isRenamable = old.isRenamable;
    replacement.tiedTo = old.tiedTo;
    return replacement;
}

MachineOperand replacementRegister(const MachineOperand &old, PhysReg reg) {
    MachineOperand replacement =
        MachineOperand::physReg(reg, old.regClass(), old.isDef,
                                old.isImplicit);
    replacement.isKill = old.isKill;
    replacement.isDead = old.isDead;
    replacement.isUndef = old.isUndef;
    replacement.isEarlyClobber = old.isEarlyClobber;
    replacement.isRenamable = old.isRenamable;
    replacement.tiedTo = old.tiedTo;
    return replacement;
}

bool sameRegisterBank(RegClass lhs, RegClass rhs) {
    bool lhsGPR = lhs == RegClass::GPR32 || lhs == RegClass::GPR64;
    bool rhsGPR = rhs == RegClass::GPR32 || rhs == RegClass::GPR64;
    bool lhsVector = lhs == RegClass::FPR32 || lhs == RegClass::NEON128;
    bool rhsVector = rhs == RegClass::FPR32 || rhs == RegClass::NEON128;
    return lhsGPR && rhsGPR || lhsVector && rhsVector;
}

unsigned spillSize(RegClass regClass) {
    return regClass == RegClass::NEON128 ? 16
         : regClass == RegClass::GPR64 ? 8
                                       : 4;
}

unsigned spillAlignment(RegClass regClass) {
    return regClass == RegClass::NEON128 ? 16
         : regClass == RegClass::GPR64 ? 8
                                       : 4;
}

void computeMachineLoopDepths(MachineFunction &function) {
    using BlockSet = std::unordered_set<MachineBasicBlock *>;
    std::vector<MachineBasicBlock *> blocks;
    for (const auto &owned : function.blocks()) {
        owned->loopDepth = 0;
        blocks.push_back(owned.get());
    }
    if (blocks.empty())
        return;

    BlockSet reachable;
    std::vector<MachineBasicBlock *> reachWorklist = {
        function.entryBlock()};
    while (!reachWorklist.empty()) {
        MachineBasicBlock *block = reachWorklist.back();
        reachWorklist.pop_back();
        if (!block || !reachable.insert(block).second)
            continue;
        reachWorklist.insert(reachWorklist.end(),
                             block->successors().begin(),
                             block->successors().end());
    }

    std::unordered_map<MachineBasicBlock *, BlockSet> dominators;
    for (MachineBasicBlock *block : blocks) {
        if (!reachable.count(block)) {
            dominators[block].insert(block);
        } else if (block == function.entryBlock()) {
            dominators[block].insert(block);
        } else {
            dominators[block] = reachable;
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (MachineBasicBlock *block : blocks) {
            if (!reachable.count(block) ||
                block == function.entryBlock())
                continue;
            BlockSet next;
            bool first = true;
            for (MachineBasicBlock *predecessor :
                 block->predecessors()) {
                if (!reachable.count(predecessor))
                    continue;
                if (first) {
                    next = dominators[predecessor];
                    first = false;
                    continue;
                }
                for (auto it = next.begin();
                     it != next.end();) {
                    if (!dominators[predecessor].count(*it))
                        it = next.erase(it);
                    else
                        ++it;
                }
            }
            next.insert(block);
            if (next != dominators[block]) {
                dominators[block] = std::move(next);
                changed = true;
            }
        }
    }

    std::unordered_map<MachineBasicBlock *, BlockSet> naturalLoops;
    for (MachineBasicBlock *tail : blocks) {
        if (!reachable.count(tail))
            continue;
        for (MachineBasicBlock *header : tail->successors()) {
            if (!dominators[tail].count(header))
                continue;
            BlockSet &loop = naturalLoops[header];
            if (loop.empty())
                loop.insert(header);
            if (loop.insert(tail).second) {
                std::vector<MachineBasicBlock *> worklist = {tail};
                while (!worklist.empty()) {
                    MachineBasicBlock *current =
                        worklist.back();
                    worklist.pop_back();
                    for (MachineBasicBlock *predecessor :
                         current->predecessors())
                        if (reachable.count(predecessor) &&
                            loop.insert(predecessor).second &&
                            predecessor != header)
                            worklist.push_back(predecessor);
                }
            }
        }
    }
    for (const auto &[header, loop] : naturalLoops) {
        (void)header;
        for (MachineBasicBlock *block : loop)
            ++block->loopDepth;
    }
}

} // namespace

LivenessResult MachineLiveness::run(MachineFunction &function) const {
    computeMachineLoopDepths(function);
    using RegSet = std::set<VReg>;
    std::unordered_map<MachineBasicBlock *, RegSet> uses;
    std::unordered_map<MachineBasicBlock *, RegSet> defs;
    std::unordered_map<MachineBasicBlock *, RegSet> liveIn;
    std::unordered_map<MachineBasicBlock *, RegSet> liveOut;
    std::unordered_map<MachineBasicBlock *, unsigned> blockStart;
    std::unordered_map<MachineBasicBlock *, unsigned> blockEnd;
    std::unordered_map<const MachineInstr *, unsigned> slots;

    unsigned slot = 2;
    for (auto &owned : function.blocks()) {
        MachineBasicBlock *block = owned.get();
        blockStart[block] = slot;
        for (MachineInstr &instruction : block->instructions()) {
            instruction.slotIndex = slot;
            slots[&instruction] = slot;
            for (const MachineOperand &operand : instruction.operands()) {
                if (!operand.isVirtualRegister())
                    continue;
                VReg reg = operand.virtualRegister();
                if (operand.isDef) {
                    defs[block].insert(reg);
                } else if (!defs[block].count(reg)) {
                    uses[block].insert(reg);
                }
            }
            slot += 2;
        }
        blockEnd[block] = std::max(blockStart[block] + 1, slot);
        slot += 2;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto blockIt = function.blocks().rbegin();
             blockIt != function.blocks().rend(); ++blockIt) {
            MachineBasicBlock *block = blockIt->get();
            RegSet newOut;
            for (MachineBasicBlock *successor : block->successors())
                newOut.insert(liveIn[successor].begin(),
                              liveIn[successor].end());
            RegSet newIn = uses[block];
            for (VReg reg : newOut)
                if (!defs[block].count(reg))
                    newIn.insert(reg);
            if (newOut != liveOut[block] || newIn != liveIn[block]) {
                liveOut[block] = std::move(newOut);
                liveIn[block] = std::move(newIn);
                changed = true;
            }
        }
    }

    std::unordered_set<VReg> liveAcrossCalls;
    for (auto &owned : function.blocks()) {
        RegSet live = liveOut[owned.get()];
        for (auto it = owned->instructions().rbegin();
             it != owned->instructions().rend(); ++it) {
            if (it->isCall())
                liveAcrossCalls.insert(live.begin(), live.end());
            for (const MachineOperand &operand : it->operands())
                if (operand.isVirtualRegister() &&
                    operand.isDef)
                    live.erase(operand.virtualRegister());
            for (const MachineOperand &operand : it->operands())
                if (operand.isVirtualRegister() &&
                    !operand.isDef)
                    live.insert(operand.virtualRegister());
        }
    }

    struct MutableInterval {
        unsigned start = std::numeric_limits<unsigned>::max();
        unsigned end = 0;
        double weight = 0.0;
    };
    std::unordered_map<VReg, MutableInterval> mutableIntervals;
    for (auto &owned : function.blocks()) {
        MachineBasicBlock *block = owned.get();
        double blockWeight =
            std::pow(10.0, std::min(block->loopDepth, 4U));
        for (const MachineInstr &instruction : block->instructions()) {
            unsigned instructionSlot = slots.at(&instruction);
            for (const MachineOperand &operand : instruction.operands()) {
                if (!operand.isVirtualRegister())
                    continue;
                auto &interval =
                    mutableIntervals[operand.virtualRegister()];
                interval.start = std::min(interval.start, instructionSlot);
                interval.end =
                    std::max(interval.end, instructionSlot + 1);
                if (!operand.isDef)
                    interval.weight += blockWeight;
            }
        }
        for (VReg reg : liveIn[block]) {
            auto &interval = mutableIntervals[reg];
            interval.start = std::min(interval.start, blockStart[block]);
        }
        for (VReg reg : liveOut[block]) {
            auto &interval = mutableIntervals[reg];
            interval.end = std::max(interval.end, blockEnd[block]);
        }
    }

    LivenessResult result;
    result.intervals.reserve(mutableIntervals.size());
    for (const auto &[reg, interval] : mutableIntervals) {
        if (interval.start == std::numeric_limits<unsigned>::max())
            continue;
        result.intervals.push_back(LiveInterval{
            reg, function.registerInfo().get(reg).regClass,
            interval.start, std::max(interval.end, interval.start + 1),
            std::max(1.0, interval.weight),
            liveAcrossCalls.count(reg) != 0});
    }

    result.blockLiveOut = std::move(liveOut);

    function.setProperty(MachineProperty::TracksLiveness);
    return result;
}

bool PhiElimination::run(MachineFunction &function) const {
    struct Edge {
        MachineBasicBlock *predecessor;
        MachineBasicBlock *successor;
    };
    std::vector<Edge> edgesToSplit;
    for (const auto &successor : function.blocks()) {
        bool hasPhi = !successor->instructions().empty() &&
                      successor->instructions().front().opcode() ==
                          Opcode::PHI;
        if (!hasPhi)
            continue;
        for (MachineBasicBlock *predecessor : successor->predecessors())
            if (predecessor->successors().size() > 1)
                edgesToSplit.push_back({predecessor, successor.get()});
    }

    unsigned splitNumber = 0;
    for (const Edge &edge : edgesToSplit) {
        MachineBasicBlock &split = function.createBlock(
            "phi.edge." + std::to_string(splitNumber++));
        edge.predecessor->removeSuccessor(edge.successor);
        edge.predecessor->addSuccessor(&split);
        split.addSuccessor(edge.successor);

        for (MachineInstr &instruction :
             edge.predecessor->instructions()) {
            if (!instruction.isBranch())
                continue;
            for (MachineOperand &operand : instruction.operands())
                if (operand.kind() == MachineOperand::Kind::BasicBlock &&
                    operand.basicBlock() == edge.successor)
                    operand = MachineOperand::block(&split);
        }

        MachineInstr branch(Opcode::B);
        branch.addOperand(MachineOperand::block(edge.successor));
        split.append(std::move(branch));

        for (MachineInstr &instruction :
             edge.successor->instructions()) {
            if (instruction.opcode() != Opcode::PHI)
                break;
            for (std::size_t i = 2; i < instruction.operands().size();
                 i += 2)
                if (instruction.operands()[i].kind() ==
                        MachineOperand::Kind::BasicBlock &&
                    instruction.operands()[i].basicBlock() ==
                        edge.predecessor)
                    instruction.operands()[i] =
                        MachineOperand::block(&split);
        }
    }

    struct Copy {
        VReg destination;
        VReg source;
        RegClass regClass;
    };
    std::unordered_map<MachineBasicBlock *, std::vector<Copy>> copies;
    bool changed = false;
    for (auto &owned : function.blocks()) {
        MachineBasicBlock &successor = *owned;
        auto it = successor.instructions().begin();
        while (it != successor.instructions().end() &&
               it->opcode() == Opcode::PHI) {
            if (it->operands().empty() ||
                !it->operands()[0].isVirtualRegister() ||
                !it->operands()[0].isDef)
                throw std::logic_error("malformed Machine PHI");
            VReg destination = it->operands()[0].virtualRegister();
            RegClass regClass = it->operands()[0].regClass();
            for (std::size_t i = 1; i + 1 < it->operands().size();
                 i += 2) {
                if (!it->operands()[i].isVirtualRegister() ||
                    it->operands()[i + 1].kind() !=
                        MachineOperand::Kind::BasicBlock)
                    throw std::logic_error("malformed Machine PHI incoming");
                copies[it->operands()[i + 1].basicBlock()].push_back(
                    Copy{destination,
                         it->operands()[i].virtualRegister(), regClass});
            }
            it = successor.instructions().erase(it);
            changed = true;
        }
    }

    for (auto &[block, pending] : copies) {
        auto insertion = std::find_if(
            block->instructions().begin(), block->instructions().end(),
            [](const MachineInstr &instruction) {
                return instruction.isTerminator();
            });
        while (!pending.empty()) {
            std::unordered_set<VReg> sources;
            for (const Copy &copy : pending)
                sources.insert(copy.source);
            auto ready = std::find_if(
                pending.begin(), pending.end(), [&](const Copy &copy) {
                    return copy.destination == copy.source ||
                           !sources.count(copy.destination);
                });
            if (ready != pending.end()) {
                Copy copy = *ready;
                pending.erase(ready);
                if (copy.destination == copy.source)
                    continue;
                MachineInstr instruction(Opcode::COPY);
                instruction
                    .addOperand(MachineOperand::vreg(
                        copy.destination, copy.regClass, true))
                    .addOperand(MachineOperand::vreg(
                        copy.source, copy.regClass));
                block->instructions().insert(insertion,
                                             std::move(instruction));
                continue;
            }

            Copy cycle = pending.front();
            VReg temporary = function.registerInfo().createVirtualRegister(
                cycle.regClass,
                function.registerInfo().get(cycle.source).valueType);
            MachineInstr save(Opcode::COPY);
            save.addOperand(MachineOperand::vreg(
                                temporary, cycle.regClass, true))
                .addOperand(MachineOperand::vreg(
                    cycle.source, cycle.regClass));
            block->instructions().insert(insertion, std::move(save));
            for (Copy &copy : pending)
                if (copy.source == cycle.source)
                    copy.source = temporary;
        }
    }

    if (changed) {
        function.clearProperty(MachineProperty::IsSSA);
        function.clearProperty(MachineProperty::HasPHIs);
        function.clearProperty(MachineProperty::TracksLiveness);
    }
    return changed;
}

bool GraphColoringRegisterAllocator::colorOnce(
    MachineFunction &function, const LivenessResult &liveness,
    std::unordered_map<VReg, PhysReg> &assignments,
    std::vector<VReg> &spills) const {
    std::unordered_map<VReg, LiveInterval> intervalFor;
    std::unordered_map<VReg, std::unordered_map<VReg, double>>
        affinities;
    std::unordered_map<VReg, VReg> tiedPairs;  // tied use -> def
    std::unordered_map<VReg, VReg> tiedDefs;  // tied def -> use
    std::unordered_map<VReg, std::vector<PhysReg>> physicalHints;
    std::unordered_map<VReg, std::unordered_set<PhysReg>> forbiddenColors;
    std::vector<VReg> graphNodes;
    graphNodes.reserve(liveness.intervals.size());
    VReg maximumVReg = 0;
    for (const LiveInterval &interval : liveness.intervals)
        maximumVReg = std::max(maximumVReg, interval.reg);
    constexpr std::size_t kNoGraphIndex =
        std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> graphIndex(
        static_cast<std::size_t>(maximumVReg) + 1,
        kNoGraphIndex);
    for (const LiveInterval &interval : liveness.intervals) {
        intervalFor.emplace(interval.reg, interval);
        graphIndex[interval.reg] = graphNodes.size();
        graphNodes.push_back(interval.reg);
    }

    // Interference becomes dense in valid programs with hundreds of
    // simultaneously-live values.  Per-edge tree/hash nodes make that case
    // allocation-bound, so store the same undirected Chaitin-Briggs graph as
    // a compact bit matrix.  This also gives constant-time duplicate checks.
    const std::size_t graphWords = (graphNodes.size() + 63) / 64;
    std::vector<std::uint64_t> interference(
        graphNodes.size() * graphWords, 0);
    std::vector<unsigned> graphDegree(graphNodes.size(), 0);

    auto addEdge = [&](VReg lhs, VReg rhs) {
        if (lhs == rhs || lhs >= graphIndex.size() ||
            rhs >= graphIndex.size())
            return;
        std::size_t lhsIndex = graphIndex[lhs];
        std::size_t rhsIndex = graphIndex[rhs];
        if (lhsIndex == kNoGraphIndex ||
            rhsIndex == kNoGraphIndex)
            return;
        if (!sameRegisterBank(
                liveness.intervals[lhsIndex].regClass,
                liveness.intervals[rhsIndex].regClass))
            return;
        std::size_t lhsWord =
            lhsIndex * graphWords + rhsIndex / 64;
        std::uint64_t rhsBit =
            std::uint64_t{1} << (rhsIndex % 64);
        if (interference[lhsWord] & rhsBit)
            return;
        interference[lhsWord] |= rhsBit;
        interference[rhsIndex * graphWords + lhsIndex / 64] |=
            std::uint64_t{1} << (lhsIndex % 64);
        ++graphDegree[lhsIndex];
        ++graphDegree[rhsIndex];
    };

    auto hasEdge = [&](VReg lhs, VReg rhs) {
        if (lhs >= graphIndex.size() ||
            rhs >= graphIndex.size())
            return false;
        std::size_t lhsIndex = graphIndex[lhs];
        std::size_t rhsIndex = graphIndex[rhs];
        if (lhsIndex == kNoGraphIndex ||
            rhsIndex == kNoGraphIndex)
            return false;
        return (interference[
                    lhsIndex * graphWords + rhsIndex / 64] &
                (std::uint64_t{1} << (rhsIndex % 64))) != 0;
    };

    auto forEachNeighbor = [&](VReg reg, auto &&callback) {
        if (reg >= graphIndex.size() ||
            graphIndex[reg] == kNoGraphIndex)
            return;
        const std::uint64_t *row =
            interference.data() + graphIndex[reg] * graphWords;
        for (std::size_t wordIndex = 0;
             wordIndex < graphWords; ++wordIndex) {
            std::uint64_t word = row[wordIndex];
            while (word) {
                unsigned bit =
                    static_cast<unsigned>(__builtin_ctzll(word));
                std::size_t neighborIndex =
                    wordIndex * 64 + bit;
                if (neighborIndex < graphNodes.size())
                    callback(graphNodes[neighborIndex]);
                word &= word - 1;
            }
        }
    };

    for (const auto &owned : function.blocks()) {
        for (const MachineInstr &instruction : owned->instructions()) {
            std::vector<VReg> defs;
            std::vector<VReg> uses;
            for (const MachineOperand &operand : instruction.operands()) {
                if (operand.isVirtualRegister()) {
                    (operand.isDef ? defs : uses)
                        .push_back(operand.virtualRegister());
                }
            }

            VReg copyDef = 0;
            VReg copyUse = 0;
            if (instruction.opcode() == Opcode::COPY &&
                defs.size() == 1 && uses.size() == 1) {
                copyDef = defs.front();
                copyUse = uses.front();
                double affinityWeight = std::pow(
                    10.0, std::min(owned->loopDepth, 4U));
                affinities[copyDef][copyUse] += affinityWeight;
                affinities[copyUse][copyDef] += affinityWeight;
            } else if (instruction.opcode() == Opcode::COPY &&
                       defs.size() == 1 &&
                       instruction.operands().size() >= 2 &&
                       instruction.operands()[1].isPhysicalRegister()) {
                physicalHints[defs.front()].push_back(
                    instruction.operands()[1].physicalRegister());
            } else if (instruction.opcode() == Opcode::COPY &&
                       uses.size() == 1 &&
                       !instruction.operands().empty() &&
                       instruction.operands()[0].isPhysicalRegister()) {
                physicalHints[uses.front()].push_back(
                    instruction.operands()[0].physicalRegister());
            }

            for (std::size_t i = 0; i < defs.size(); ++i)
                for (std::size_t j = i + 1; j < defs.size(); ++j)
                    addEdge(defs[i], defs[j]);
            for (std::size_t i = 0; i < uses.size(); ++i)
                for (std::size_t j = i + 1; j < uses.size(); ++j)
                    addEdge(uses[i], uses[j]);

            // Tied operands (e.g. fmla/fmls accumulate into vd in place):
            // the tied use must share the destination register, while the
            // destination must not collide with any other source of the
            // same instruction (vd is read-modify-write).
            for (std::size_t i = 0; i < instruction.operands().size(); ++i) {
                const MachineOperand &operand = instruction.operands()[i];
                if (!operand.isVirtualRegister() || operand.isDef ||
                    operand.tiedTo < 0 ||
                    static_cast<std::size_t>(operand.tiedTo) >=
                        instruction.operands().size())
                    continue;
                const MachineOperand &defOperand =
                    instruction.operands()[operand.tiedTo];
                if (!defOperand.isVirtualRegister() || !defOperand.isDef)
                    continue;
                VReg tiedDef = defOperand.virtualRegister();
                VReg tiedUse = operand.virtualRegister();
                tiedPairs[tiedUse] = tiedDef;
                tiedDefs[tiedDef] = tiedUse;
                for (std::size_t j = 0; j < instruction.operands().size(); ++j) {
                    const MachineOperand &other = instruction.operands()[j];
                    if (other.isVirtualRegister() && !other.isDef &&
                        i != j &&
                        static_cast<std::size_t>(operand.tiedTo) != j)
                        addEdge(tiedDef, other.virtualRegister());
                }
            }

            for (std::size_t defIndex = 0;
                 defIndex < instruction.operands().size(); ++defIndex) {
                const MachineOperand &operand =
                    instruction.operands()[defIndex];
                if (!operand.isVirtualRegister() || !operand.isEarlyClobber ||
                    !operand.isDef)
                    continue;
                for (std::size_t useIndex = 0;
                     useIndex < instruction.operands().size(); ++useIndex) {
                    const MachineOperand &use =
                        instruction.operands()[useIndex];
                    if (!use.isVirtualRegister() || use.isDef ||
                        use.tiedTo == static_cast<int>(defIndex))
                        continue;
                    addEdge(operand.virtualRegister(),
                            use.virtualRegister());
                }
            }
        }
    }

    // Build def/live interference in a reverse walk.  Keeping only block
    // live-out sets avoids materializing an O(instructions × live-values)
    // table for large straight-line functions.
    for (const auto &owned : function.blocks()) {
        std::set<VReg> live;
        auto blockLive = liveness.blockLiveOut.find(owned.get());
        if (blockLive != liveness.blockLiveOut.end())
            live = blockLive->second;
        for (auto it = owned->instructions().rbegin();
             it != owned->instructions().rend(); ++it) {
            std::vector<VReg> defs;
            std::vector<VReg> uses;
            std::vector<PhysReg> physicalDefs;
            std::vector<PhysReg> physicalUses;
            for (const MachineOperand &operand : it->operands()) {
                if (operand.isVirtualRegister())
                    (operand.isDef ? defs : uses)
                        .push_back(operand.virtualRegister());
                else if (operand.isPhysicalRegister())
                    (operand.isDef ? physicalDefs : physicalUses)
                        .push_back(operand.physicalRegister());
            }

            VReg copyDef = 0;
            VReg copyUse = 0;
            if (it->opcode() == Opcode::COPY &&
                defs.size() == 1 && uses.size() == 1) {
                copyDef = defs.front();
                copyUse = uses.front();
            }
            for (VReg def : defs) {
                for (VReg liveReg : live) {
                    if (def == copyDef && liveReg == copyUse)
                        continue;
                    addEdge(def, liveReg);
                }
            }
            for (PhysReg physical : physicalDefs) {
                for (VReg liveReg : live) {
                    if (it->opcode() == Opcode::COPY &&
                        liveReg == copyUse)
                        continue;
                    forbiddenColors[liveReg].insert(physical);
                }
            }
            // A fixed physical use also occupies its register at this
            // instruction.  This matters after pre-RA scheduling: a
            // virtual value live across `vreg = COPY phys` must not be
            // colored to phys, or it would overwrite the incoming physical
            // value before the copy.  The copy destination itself may still
            // coalesce with its physical source.
            for (PhysReg physical : physicalUses) {
                for (VReg liveReg : live) {
                    if (it->opcode() == Opcode::COPY &&
                        liveReg == copyDef)
                        continue;
                    forbiddenColors[liveReg].insert(physical);
                }
                for (VReg used : uses)
                    forbiddenColors[used].insert(physical);
            }
            for (VReg def : defs)
                live.erase(def);
            live.insert(uses.begin(), uses.end());
        }
    }

    // ABI argument moves are parallel assignments represented as ordered
    // COPYs.  Precolor conflicts make that order safe even after spill code
    // is inserted between members of the group: an early destination may
    // not overwrite a physical source that has not been consumed yet.
    for (const auto &owned : function.blocks()) {
        std::unordered_map<unsigned, std::vector<const MachineInstr *>>
            copyGroups;
        for (const MachineInstr &instruction : owned->instructions())
            if (instruction.parallelCopyGroup)
                copyGroups[instruction.parallelCopyGroup].push_back(
                    &instruction);

        for (const auto &[group, copies] : copyGroups) {
            (void)group;
            std::set<PhysReg> earlierPhysicalDefs;
            for (const MachineInstr *copy : copies) {
                if (copy->opcode() != Opcode::COPY ||
                    copy->operands().size() != 2)
                    throw std::logic_error(
                        "malformed virtual parallel copy");
                const MachineOperand &destination = copy->operands()[0];
                const MachineOperand &source = copy->operands()[1];
                if (destination.isPhysicalRegister() &&
                    source.isVirtualRegister()) {
                    forbiddenColors[source.virtualRegister()].insert(
                        earlierPhysicalDefs.begin(),
                        earlierPhysicalDefs.end());
                    earlierPhysicalDefs.insert(
                        destination.physicalRegister());
                }
            }

            std::multiset<PhysReg> remainingPhysicalUses;
            for (const MachineInstr *copy : copies) {
                const MachineOperand &source = copy->operands()[1];
                if (source.isPhysicalRegister())
                    remainingPhysicalUses.insert(
                        source.physicalRegister());
            }
            for (const MachineInstr *copy : copies) {
                const MachineOperand &destination =
                    copy->operands()[0];
                const MachineOperand &source = copy->operands()[1];
                if (source.isPhysicalRegister()) {
                    auto current = remainingPhysicalUses.find(
                        source.physicalRegister());
                    remainingPhysicalUses.erase(current);
                }
                if (!destination.isVirtualRegister())
                    continue;
                forbiddenColors[destination.virtualRegister()].insert(
                    remainingPhysicalUses.begin(),
                    remainingPhysicalUses.end());
            }
        }
    }

    auto colorBank = [&](bool vectorBank) {
        std::vector<VReg> nodes;
        for (const auto &[reg, interval] : intervalFor) {
            bool isVector = interval.regClass == RegClass::FPR32 ||
                            interval.regClass == RegClass::NEON128;
            if (isVector == vectorBank)
                nodes.push_back(reg);
        }
        std::sort(nodes.begin(), nodes.end());
        if (nodes.empty())
            return;

        std::unordered_map<VReg, unsigned> degree;
        std::unordered_map<VReg, unsigned> availableColorCount;
        std::unordered_set<VReg> remaining(nodes.begin(), nodes.end());
        std::set<VReg> lowDegree;
        for (VReg reg : nodes) {
            degree[reg] = graphDegree[graphIndex[reg]];
            const LiveInterval &interval = intervalFor.at(reg);
            unsigned availableColors = 0;
            for (PhysReg physical :
                 RegisterInfo::allocationOrder(interval.regClass)) {
                if (RegisterInfo::isReserved(physical) ||
                    forbiddenColors[reg].count(physical))
                    continue;
                if (interval.crossesCall &&
                    interval.regClass == RegClass::NEON128)
                    continue;
                if (interval.crossesCall &&
                    RegisterInfo::isCallerSaved(physical))
                    continue;
                ++availableColors;
            }
            availableColorCount[reg] = availableColors;
            if (degree[reg] < availableColors)
                lowDegree.insert(reg);
        }

        std::vector<VReg> simplifyStack;
        std::unordered_set<VReg> potentialSpills;
        while (!remaining.empty()) {
            VReg selected = 0;
            if (!lowDegree.empty()) {
                selected = *lowDegree.begin();
                lowDegree.erase(lowDegree.begin());
            }

            if (!selected) {
                double bestCost = std::numeric_limits<double>::infinity();
                for (VReg reg : nodes) {
                    if (!remaining.count(reg))
                        continue;
                    const LiveInterval &interval = intervalFor.at(reg);
                    double cost =
                        function.registerInfo().get(reg).spillTemporary
                            ? std::numeric_limits<double>::infinity()
                            : interval.weight /
                                  static_cast<double>(degree[reg] + 1);
                    if (cost < bestCost ||
                        (cost == bestCost && (!selected || reg < selected))) {
                        bestCost = cost;
                        selected = reg;
                    }
                }
                potentialSpills.insert(selected);
            }

            remaining.erase(selected);
            lowDegree.erase(selected);
            simplifyStack.push_back(selected);
            forEachNeighbor(selected, [&](VReg neighbor) {
                if (remaining.count(neighbor) && degree[neighbor] > 0) {
                    --degree[neighbor];
                    if (degree[neighbor] <
                        availableColorCount[neighbor])
                        lowDegree.insert(neighbor);
                }
            });
        }

        while (!simplifyStack.empty()) {
            VReg reg = simplifyStack.back();
            simplifyStack.pop_back();
            const LiveInterval &interval = intervalFor.at(reg);
            std::set<PhysReg> unavailable;
            forEachNeighbor(reg, [&](VReg neighbor) {
                auto assigned = assignments.find(neighbor);
                if (assigned != assignments.end())
                    unavailable.insert(assigned->second);
            });
            auto allowed = [&](PhysReg physical) {
                return !RegisterInfo::isReserved(physical) &&
                       !unavailable.count(physical) &&
                       !forbiddenColors[reg].count(physical) &&
                       !(interval.crossesCall &&
                         interval.regClass == RegClass::NEON128) &&
                       (!interval.crossesCall ||
                        !RegisterInfo::isCallerSaved(physical));
            };

            PhysReg selected = PhysReg::NoReg;
            // Tied pairs must share a register: prefer the partner's color
            // (the partner cannot be an interference neighbor by
            // construction, so its color is always available).
            {
                VReg partner = 0;
                auto tiedUse = tiedPairs.find(reg);
                if (tiedUse != tiedPairs.end())
                    partner = tiedUse->second;
                auto tiedDef = tiedDefs.find(reg);
                if (!partner && tiedDef != tiedDefs.end())
                    partner = tiedDef->second;
                if (partner) {
                    auto assigned = assignments.find(partner);
                    if (assigned != assignments.end() &&
                        allowed(assigned->second))
                        selected = assigned->second;
                }
            }
            auto hints = physicalHints.find(reg);
            if (hints != physicalHints.end()) {
                for (PhysReg hint : hints->second)
                    if (allowed(hint)) {
                        selected = hint;
                        break;
                    }
            }
            if (selected == PhysReg::NoReg) {
                std::vector<std::pair<VReg, double>>
                    weightedAffinities(
                        affinities[reg].begin(),
                        affinities[reg].end());
                std::sort(
                    weightedAffinities.begin(),
                    weightedAffinities.end(),
                    [](const auto &lhs, const auto &rhs) {
                        return lhs.second != rhs.second
                                   ? lhs.second > rhs.second
                                   : lhs.first < rhs.first;
                    });
                for (const auto &[affinity, weight] :
                     weightedAffinities) {
                    (void)weight;
                    auto assigned = assignments.find(affinity);
                    if (assigned != assignments.end() &&
                        allowed(assigned->second)) {
                        selected = assigned->second;
                        break;
                    }
                }
            }
            if (selected == PhysReg::NoReg) {
                for (PhysReg candidate :
                     RegisterInfo::allocationOrder(interval.regClass))
                    if (allowed(candidate)) {
                        selected = candidate;
                        break;
                    }
            }
            if (selected == PhysReg::NoReg)
                spills.push_back(reg);
            else
                assignments.emplace(reg, selected);
        }
    };

    colorBank(false);
    colorBank(true);
    std::sort(spills.begin(), spills.end());
    spills.erase(std::unique(spills.begin(), spills.end()), spills.end());
    if (!spills.empty())
        return false;

    // Optimistic recoloring completes the graph-coloring copy-coalescing
    // step.  It joins a non-interfering COPY pair whenever one endpoint can
    // adopt the other's color without changing any neighbor assignment.
    struct AffinityEdge {
        VReg lhs = 0;
        VReg rhs = 0;
        double weight = 0.0;
    };
    std::vector<AffinityEdge> affinityEdges;
    for (const auto &[reg, partners] : affinities)
        for (const auto &[partner, weight] : partners)
            if (reg < partner)
                affinityEdges.push_back(
                    AffinityEdge{reg, partner, weight});
    std::sort(
        affinityEdges.begin(), affinityEdges.end(),
        [](const AffinityEdge &lhs,
           const AffinityEdge &rhs) {
            if (lhs.weight != rhs.weight)
                return lhs.weight > rhs.weight;
            if (lhs.lhs != rhs.lhs)
                return lhs.lhs < rhs.lhs;
            return lhs.rhs < rhs.rhs;
        });
    auto strongestSatisfiedAffinity = [&](VReg reg) {
        double weight = 0.0;
        auto assigned = assignments.find(reg);
        if (assigned == assignments.end())
            return weight;
        for (const auto &[partner, candidateWeight] :
             affinities[reg]) {
            auto partnerAssignment =
                assignments.find(partner);
            if (partnerAssignment != assignments.end() &&
                partnerAssignment->second == assigned->second)
                weight = std::max(weight, candidateWeight);
        }
        return weight;
    };

    for (unsigned iteration = 0; iteration < 4; ++iteration) {
        bool changed = false;
        for (const AffinityEdge &edge : affinityEdges) {
            VReg reg = edge.lhs;
            VReg partner = edge.rhs;
            if (hasEdge(reg, partner) ||
                !assignments.count(reg) ||
                !assignments.count(partner) ||
                assignments[reg] == assignments[partner])
                continue;
                auto canRecolor = [&](VReg value, PhysReg color) {
                    const LiveInterval &interval =
                        intervalFor.at(value);
                    if (RegisterInfo::isReserved(color) ||
                        forbiddenColors[value].count(color) ||
                        (interval.crossesCall &&
                         interval.regClass == RegClass::NEON128) ||
                        (interval.crossesCall &&
                         RegisterInfo::isCallerSaved(color)))
                        return false;
                    bool conflict = false;
                    forEachNeighbor(value, [&](VReg neighbor) {
                        if (assignments.count(neighbor) &&
                            assignments[neighbor] == color)
                            conflict = true;
                    });
                    return !conflict;
                };
                if (edge.weight >=
                        strongestSatisfiedAffinity(reg) &&
                    canRecolor(reg, assignments[partner])) {
                    assignments[reg] = assignments[partner];
                    changed = true;
                } else if (
                    edge.weight >=
                        strongestSatisfiedAffinity(partner) &&
                    canRecolor(partner, assignments[reg])) {
                    assignments[partner] = assignments[reg];
                    changed = true;
                }
        }
        if (!changed)
            break;
    }
    return true;
}

void GraphColoringRegisterAllocator::insertSpills(
    MachineFunction &function, const std::vector<VReg> &spills,
    std::unordered_map<VReg, int> &spillSlots) const {
    std::unordered_set<VReg> spilled(spills.begin(), spills.end());
    std::unordered_map<VReg, MachineInstr *> rematerializations;
    for (VReg reg : spills) {
        const VRegInfo &info = function.registerInfo().get(reg);
        MachineInstr *definition = info.definition;
        if (!definition ||
            (definition->opcode() != Opcode::MOVi32 &&
             definition->opcode() != Opcode::MOVi64 &&
             definition->opcode() != Opcode::MOVIv4Zero) ||
            definition->operands().empty() ||
            !definition->operands()[0].isVirtualRegister() ||
            definition->operands()[0].virtualRegister() != reg)
            continue;
        bool hasVirtualUse = false;
        for (const MachineOperand &operand :
             definition->operands())
            hasVirtualUse |=
                operand.isVirtualRegister() && !operand.isDef;
        if (!hasVirtualUse)
            rematerializations.emplace(reg, definition);
    }

    for (VReg reg : spills) {
        if (rematerializations.count(reg))
            continue;
        if (spillSlots.count(reg))
            continue;
        RegClass regClass = function.registerInfo().get(reg).regClass;
        spillSlots.emplace(
            reg, function.frameInfo().createStackObject(
                     spillSize(regClass), spillAlignment(regClass), true));
    }

    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        for (auto it = instructions.begin(); it != instructions.end(); ++it) {
            std::unordered_map<VReg, VReg> temporaries;
            std::unordered_set<VReg> needsLoad;
            std::unordered_set<VReg> needsStore;
            for (const MachineOperand &operand : it->operands()) {
                if (!operand.isVirtualRegister() ||
                    !spilled.count(operand.virtualRegister()))
                    continue;
                VReg old = operand.virtualRegister();
                if (!operand.isDef)
                    needsLoad.insert(old);
                else
                    needsStore.insert(old);
                if (!temporaries.count(old)) {
                    const VRegInfo &info = function.registerInfo().get(old);
                    temporaries.emplace(
                        old, function.registerInfo().createVirtualRegister(
                                 info.regClass, info.valueType));
                    function.registerInfo()
                        .get(temporaries.at(old))
                        .spillTemporary = true;
                }
            }
            if (temporaries.empty())
                continue;

            for (VReg old : needsLoad) {
                VReg temporary = temporaries.at(old);
                RegClass regClass =
                    function.registerInfo().get(old).regClass;
                auto rematerialization =
                    rematerializations.find(old);
                if (rematerialization !=
                    rematerializations.end()) {
                    MachineInstr materialized =
                        *rematerialization->second;
                    materialized.operands()[0] =
                        MachineOperand::vreg(
                            temporary, regClass, true);
                    auto inserted = instructions.insert(
                        it, std::move(materialized));
                    function.registerInfo().setDefinition(
                        temporary, &*inserted);
                    continue;
                }
                MachineInstr load(Opcode::SPILL_LOAD);
                load.addOperand(MachineOperand::vreg(
                                    temporary, regClass, true))
                    .addOperand(MachineOperand::frameIndex(
                        spillSlots.at(old)));
                load.addMemoryOperand(MachineMemOperand{
                    MachineMemOperand::Access::Load, spillSize(regClass),
                    spillAlignment(regClass), nullptr, spillSlots.at(old), 0,
                    false});
                auto inserted = instructions.insert(it, std::move(load));
                function.registerInfo().setDefinition(temporary, &*inserted);
            }

            for (MachineOperand &operand : it->operands()) {
                if (!operand.isVirtualRegister() ||
                    !spilled.count(operand.virtualRegister()))
                    continue;
                VReg old = operand.virtualRegister();
                operand = replacementRegister(
                    operand, temporaries.at(old),
                    function.registerInfo().get(old).regClass);
                if (operand.isDef)
                    function.registerInfo().setDefinition(
                        temporaries.at(old), &*it);
            }

            auto after = std::next(it);
            for (VReg old : needsStore) {
                if (rematerializations.count(old))
                    continue;
                VReg temporary = temporaries.at(old);
                RegClass regClass =
                    function.registerInfo().get(old).regClass;
                MachineInstr store(Opcode::SPILL_STORE);
                store.addOperand(MachineOperand::vreg(
                                     temporary, regClass))
                    .addOperand(MachineOperand::frameIndex(
                        spillSlots.at(old)));
                store.addMemoryOperand(MachineMemOperand{
                    MachineMemOperand::Access::Store, spillSize(regClass),
                    spillAlignment(regClass), nullptr, spillSlots.at(old), 0,
                    false});
                instructions.insert(after, std::move(store));
            }
        }
    }

    // Each use now has its own short-lived materialization, so the original
    // long-lived definition is dead and must not consume a color in the next
    // allocation round.
    std::unordered_set<MachineInstr *> deadDefinitions;
    for (const auto &[reg, definition] : rematerializations) {
        (void)reg;
        deadDefinitions.insert(definition);
    }
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        for (auto it = instructions.begin();
             it != instructions.end();) {
            if (!deadDefinitions.count(&*it)) {
                ++it;
                continue;
            }
            it = instructions.erase(it);
        }
    }
    function.clearProperty(MachineProperty::TracksLiveness);
}

void GraphColoringRegisterAllocator::rewriteVirtualRegisters(
    MachineFunction &function,
    const std::unordered_map<VReg, PhysReg> &assignments) const {
    for (auto &owned : function.blocks()) {
        for (MachineInstr &instruction : owned->instructions()) {
            for (MachineOperand &operand : instruction.operands()) {
                if (!operand.isVirtualRegister())
                    continue;
                auto assignment =
                    assignments.find(operand.virtualRegister());
                if (assignment == assignments.end())
                    throw std::logic_error(
                        "register allocator left an unassigned vreg");
                operand = replacementRegister(operand,
                                              assignment->second);
            }
        }
    }
    function.setProperty(MachineProperty::NoVRegs);
    function.clearProperty(MachineProperty::IsSSA);
    function.clearProperty(MachineProperty::HasPHIs);
    function.clearProperty(MachineProperty::TracksLiveness);
}

void GraphColoringRegisterAllocator::run(MachineFunction &function) const {
    MachineLiveness analysis;
    std::unordered_map<VReg, int> spillSlots;
    constexpr unsigned kMaximumSpillRounds = 32;
    for (unsigned round = 0; round < kMaximumSpillRounds; ++round) {
        LivenessResult liveness = analysis.run(function);
        std::unordered_map<VReg, PhysReg> assignments;
        std::vector<VReg> spills;
        if (colorOnce(function, liveness, assignments, spills)) {
            rewriteVirtualRegisters(function, assignments);
            return;
        }
        insertSpills(function, spills, spillSlots);
    }
    throw std::logic_error(
        "register allocation exceeded the bounded spill iteration limit");
}

bool PostRAParallelCopyResolver::run(MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::NoVRegs))
        throw std::logic_error(
            "parallel physical copies require completed allocation");

    struct Copy {
        PhysReg destination = PhysReg::NoReg;
        PhysReg source = PhysReg::NoReg;
        RegClass regClass = RegClass::Invalid;
    };
    bool changed = false;
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (!it->parallelCopyGroup) {
                ++it;
                continue;
            }

            const unsigned group = it->parallelCopyGroup;
            std::vector<Copy> pending;
            while (it != instructions.end() &&
                   it->parallelCopyGroup == group) {
                if (it->opcode() != Opcode::COPY ||
                    it->operands().size() != 2 ||
                    !it->operands()[0].isPhysicalRegister() ||
                    !it->operands()[1].isPhysicalRegister())
                    throw std::logic_error(
                        "malformed allocated parallel copy");
                pending.push_back(Copy{
                    it->operands()[0].physicalRegister(),
                    it->operands()[1].physicalRegister(),
                    it->operands()[0].regClass()});
                it = instructions.erase(it);
            }
            auto insertion = it;

            auto emitCopy = [&](const Copy &copy) {
                if (copy.destination == copy.source)
                    return;
                MachineInstr instruction(Opcode::COPY);
                instruction
                    .addOperand(MachineOperand::physReg(
                        copy.destination, copy.regClass, true))
                    .addOperand(MachineOperand::physReg(
                        copy.source, copy.regClass));
                instructions.insert(insertion, std::move(instruction));
            };

            while (!pending.empty()) {
                std::unordered_set<PhysReg> sources;
                for (const Copy &copy : pending)
                    sources.insert(copy.source);
                auto ready = std::find_if(
                    pending.begin(), pending.end(), [&](const Copy &copy) {
                        return copy.destination == copy.source ||
                               !sources.count(copy.destination);
                    });
                if (ready != pending.end()) {
                    Copy copy = *ready;
                    pending.erase(ready);
                    emitCopy(copy);
                    continue;
                }

                throw std::logic_error(
                    "register allocation produced an unresolved "
                    "physical parallel-copy cycle");
            }
            changed = true;
        }
    }
    return changed;
}

} // namespace backend::aarch64
