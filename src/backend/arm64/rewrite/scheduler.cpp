#include "../../../include/backend/arm64/rewrite/scheduler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace backend::aarch64 {
namespace {

using RegisterKey = std::uint64_t;

constexpr unsigned resourceIndex(SchedResource resource) {
    return static_cast<unsigned>(resource);
}

RegisterKey registerKey(const MachineOperand &operand) {
    if (operand.isPhysicalRegister())
        return (1ULL << 63) |
               static_cast<unsigned>(operand.physicalRegister());
    return operand.virtualRegister();
}

bool isVirtualRegisterKey(RegisterKey key) {
    return (key & (1ULL << 63)) == 0;
}

bool operandUsesKey(const MachineOperand &operand, RegisterKey key) {
    return operand.isRegister() && !operand.isDef &&
           registerKey(operand) == key;
}

bool isVectorRegisterClass(RegClass regClass) {
    return regClass == RegClass::FPR32 ||
           regClass == RegClass::NEON128;
}

unsigned integerConstantPieces(const MachineInstr &instruction) {
    if (instruction.operands().size() < 2 ||
        instruction.operands()[1].kind() !=
            MachineOperand::Kind::Immediate)
        return 1;
    std::uint64_t value = static_cast<std::uint64_t>(
        instruction.operands()[1].immediate());
    unsigned pieces =
        instruction.opcode() == Opcode::MOVi32 ? 2 : 4;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < pieces; ++i)
        nonzero += ((value >> (i * 16)) & 0xffffU) != 0;
    return std::max(1U, nonzero);
}

bool isIntegerALU(Opcode opcode) {
    switch (opcode) {
    case Opcode::COPY:
    case Opcode::MOVi32:
    case Opcode::MOVi64:
    case Opcode::ADRP:
    case Opcode::ADDlow:
    case Opcode::ADDWrr:
    case Opcode::ADDWri:
    case Opcode::ADDWrs:
    case Opcode::ADDWrsX:
    case Opcode::ADDWlsl:
    case Opcode::SUBWrr:
    case Opcode::SUBWri:
    case Opcode::NEGW:
    case Opcode::CNEGW:
    case Opcode::ANDWrr:
    case Opcode::ANDWri:
    case Opcode::ORRWrr:
    case Opcode::EORWrr:
    case Opcode::LSLWrr:
    case Opcode::LSLWri:
    case Opcode::LSRWrr:
    case Opcode::LSRWri:
    case Opcode::ASRWrr:
    case Opcode::ASRWri:
    case Opcode::CMPWrr:
    case Opcode::CMPWri:
    case Opcode::TSTWrr:
    case Opcode::TSTWri:
    case Opcode::CLZW:
    case Opcode::RBITW:
    case Opcode::CSELW:
    case Opcode::CSELX:
    case Opcode::CSETW:
    case Opcode::ADDXrr:
    case Opcode::ADDXri:
    case Opcode::ADDXrs:
    case Opcode::SUBXrr:
    case Opcode::SUBXri:
    case Opcode::LSLXri:
    case Opcode::ASRXri:
    case Opcode::COPYXtoW:
    case Opcode::MOVXrr:
    case Opcode::SXTW:
    case Opcode::UXTW:
        return true;
    default:
        return false;
    }
}

bool isSimpleShift(Opcode opcode) {
    switch (opcode) {
    case Opcode::LSLWri:
    case Opcode::LSRWri:
    case Opcode::ASRWri:
    case Opcode::LSLXri:
    case Opcode::ASRXri:
        return true;
    default:
        return false;
    }
}

bool isIntegerMAC(Opcode opcode) {
    switch (opcode) {
    case Opcode::MULWrr:
    case Opcode::MADDWrrr:
    case Opcode::MSUBWrrr:
    case Opcode::SMULLXrr:
    case Opcode::SMADDLXrrr:
        return true;
    default:
        return false;
    }
}

bool isIntegerDivide(Opcode opcode) {
    return opcode == Opcode::SDIVWrr ||
           opcode == Opcode::UDIVWrr;
}

bool isLoad(Opcode opcode) {
    switch (opcode) {
    case Opcode::LDRWui:
    case Opcode::LDRWlo:
    case Opcode::LDRWro:
    case Opcode::LDRWpost:
    case Opcode::LDRSui:
    case Opcode::LDRSlo:
    case Opcode::LDRSro:
    case Opcode::LDRSpost:
    case Opcode::LDRDui:
    case Opcode::LDRQui:
    case Opcode::LDRQlo:
    case Opcode::LDRQpost:
    case Opcode::LDRXui:
    case Opcode::LDRXlo:
    case Opcode::LDRXpost:
    case Opcode::LDPXi:
    case Opcode::LDPDi:
    case Opcode::LDPQi:
    case Opcode::LDPXpost:
    case Opcode::SPILL_LOAD:
        return true;
    default:
        return false;
    }
}

bool isStore(Opcode opcode) {
    switch (opcode) {
    case Opcode::STRWui:
    case Opcode::STRWlo:
    case Opcode::STRWro:
    case Opcode::STRWpost:
    case Opcode::STRSui:
    case Opcode::STRSlo:
    case Opcode::STRSro:
    case Opcode::STRSpost:
    case Opcode::STRDui:
    case Opcode::STRQui:
    case Opcode::STRQlo:
    case Opcode::STRQpost:
    case Opcode::STRXui:
    case Opcode::STRXlo:
    case Opcode::STRXpost:
    case Opcode::STPXi:
    case Opcode::STPDi:
    case Opcode::STPQi:
    case Opcode::STPXpre:
    case Opcode::SPILL_STORE:
        return true;
    default:
        return false;
    }
}

bool isPairLoad(Opcode opcode) {
    return opcode == Opcode::LDPXi || opcode == Opcode::LDPDi ||
           opcode == Opcode::LDPQi || opcode == Opcode::LDPXpost;
}

bool isFPALU(Opcode opcode) {
    switch (opcode) {
    case Opcode::MOVIv4Zero:
    case Opcode::FADDS:
    case Opcode::FSUBS:
    case Opcode::FNEGS:
    case Opcode::FCMPSrr:
    case Opcode::FCMPZS:
    case Opcode::SCVTFWS:
    case Opcode::FCVTZSW:
    case Opcode::FMOVWS:
    case Opcode::FMOVSW:
    case Opcode::FCSELS:
    case Opcode::DUPv4i32:
    case Opcode::DUPv4f32:
    case Opcode::INSv4i32:
    case Opcode::INSv4f32:
    case Opcode::EXTRACTv4i32:
    case Opcode::EXTRACTv4f32:
    case Opcode::ADDv4i32:
    case Opcode::SUBv4i32:
    case Opcode::MULv4i32:
    case Opcode::MLAv4i32:
    case Opcode::MLSv4i32:
    case Opcode::SMINv4i32:
    case Opcode::SMAXv4i32:
    case Opcode::NEGv4i32:
    case Opcode::SSHLv4i32:
    case Opcode::USHLv4i32:
    case Opcode::ADDv4f32:
    case Opcode::SUBv4f32:
    case Opcode::NEGv4f32:
    case Opcode::ANDv16i8:
    case Opcode::ORRv16i8:
    case Opcode::EORv16i8:
    case Opcode::SHUFFLEv16i8:
    case Opcode::ADDVv4i32:
        return true;
    default:
        return false;
    }
}

bool isFPMultiply(Opcode opcode) {
    switch (opcode) {
    case Opcode::FMULS:
    case Opcode::MULv4f32:
    case Opcode::FMLAv4f32:
    case Opcode::FMLSv4f32:
        return true;
    default:
        return false;
    }
}

bool isFPDivide(Opcode opcode) {
    return opcode == Opcode::FDIVS ||
           opcode == Opcode::DIVv4f32;
}

unsigned consumerReadAdvance(const MachineInstr &consumer,
                             RegisterKey key) {
    Opcode opcode = consumer.opcode();
    unsigned bestAdvance = 0;
    for (unsigned index = 0;
         index < consumer.operands().size(); ++index) {
        if (!operandUsesKey(consumer.operands()[index], key))
            continue;
        unsigned advance = 0;
        if (isIntegerALU(opcode))
            advance = 2;
        else if (isIntegerDivide(opcode))
            advance = 1;
        else if (isIntegerMAC(opcode))
            advance =
                (opcode == Opcode::MADDWrrr ||
                 opcode == Opcode::MSUBWrrr ||
                 opcode == Opcode::SMADDLXrrr) &&
                        index == 3
                    ? 2
                    : 1;
        bestAdvance = std::max(bestAdvance, advance);
    }
    return bestAdvance;
}

std::optional<std::string> directGlobal(
    const MachineInstr &instruction) {
    for (const MachineOperand &operand : instruction.operands())
        if (operand.kind() == MachineOperand::Kind::GlobalSymbol)
            return operand.symbol();
    return std::nullopt;
}

bool knownDisjointMemory(const MachineMemOperand &lhs,
                         const MachineMemOperand &rhs,
                         const MachineInstr &lhsInstruction,
                         const MachineInstr &rhsInstruction) {
    if (lhs.frameIndex && rhs.frameIndex) {
        if (*lhs.frameIndex != *rhs.frameIndex)
            return true;
        if (!lhs.offset || !rhs.offset)
            return false;
        std::int64_t lhsBegin = *lhs.offset;
        std::int64_t rhsBegin = *rhs.offset;
        std::int64_t lhsEnd =
            lhsBegin + static_cast<std::int64_t>(lhs.size);
        std::int64_t rhsEnd =
            rhsBegin + static_cast<std::int64_t>(rhs.size);
        return lhsEnd <= rhsBegin || rhsEnd <= lhsBegin;
    }

    auto lhsGlobal = directGlobal(lhsInstruction);
    auto rhsGlobal = directGlobal(rhsInstruction);
    if (lhsGlobal && rhsGlobal)
        return *lhsGlobal != *rhsGlobal;
    return false;
}

bool memoryDependency(const MachineInstr &lhs,
                      const MachineInstr &rhs) {
    if ((!lhs.mayLoad() && !lhs.mayStore()) ||
        (!rhs.mayLoad() && !rhs.mayStore()) ||
        (!lhs.mayStore() && !rhs.mayStore()))
        return false;
    if (lhs.memoryOperands().empty() ||
        rhs.memoryOperands().empty())
        return true;
    for (const MachineMemOperand &lhsMemory : lhs.memoryOperands())
        for (const MachineMemOperand &rhsMemory : rhs.memoryOperands())
            if (!knownDisjointMemory(
                    lhsMemory, rhsMemory, lhs, rhs))
                return true;
    return false;
}

bool schedulingBarrier(const MachineInstr &instruction) {
    if (instruction.isTerminator() || instruction.isCall() ||
        instruction.isPseudo() || instruction.hasSideEffects())
        return true;
    if (instruction.parallelCopyGroup)
        return true;
    for (const MachineMemOperand &memory :
         instruction.memoryOperands())
        if (memory.isVolatile)
            return true;
    return false;
}

struct Edge {
    unsigned successor = 0;
    unsigned latency = 0;
};

struct Node {
    std::set<RegisterKey> defs;
    std::set<RegisterKey> uses;
    std::vector<Edge> successors;
    unsigned predecessors = 0;
    unsigned height = 1;
    unsigned readyCycle = 0;
    unsigned issueCycle = 0;
    A53SchedInfo sched;
};

int registerPressureDelta(
    const Node &node,
    const std::unordered_map<RegisterKey, unsigned> &remainingUses,
    const std::unordered_set<RegisterKey> &live) {
    int delta = 0;
    for (RegisterKey use : node.uses) {
        if (!isVirtualRegisterKey(use))
            continue;
        auto found = remainingUses.find(use);
        if (found != remainingUses.end() && found->second == 1 &&
            live.count(use))
            --delta;
    }
    for (RegisterKey def : node.defs) {
        if (!isVirtualRegisterKey(def))
            continue;
        auto found = remainingUses.find(def);
        if (found != remainingUses.end() && found->second &&
            !live.count(def) && !node.uses.count(def))
            ++delta;
    }
    return delta;
}

struct PressureMetrics {
    unsigned peak = 0;
    std::uint64_t area = 0;
    std::set<std::pair<RegisterKey, RegisterKey>> interferences;
};

void recordInterferences(
    PressureMetrics &metrics,
    const std::unordered_set<RegisterKey> &live) {
    for (auto lhs = live.begin(); lhs != live.end(); ++lhs)
        for (auto rhs = std::next(lhs); rhs != live.end(); ++rhs)
            metrics.interferences.insert(
                std::minmax(*lhs, *rhs));
}

PressureMetrics virtualRegisterPressure(
    const std::vector<Node> &nodes,
    const std::vector<unsigned> &order) {
    std::unordered_map<RegisterKey, unsigned> remainingUses;
    std::unordered_set<RegisterKey> regionDefs;
    for (const Node &node : nodes) {
        for (RegisterKey def : node.defs)
            if (isVirtualRegisterKey(def))
                regionDefs.insert(def);
        for (RegisterKey use : node.uses)
            if (isVirtualRegisterKey(use))
                ++remainingUses[use];
    }

    std::unordered_set<RegisterKey> live;
    for (const auto &[key, count] : remainingUses)
        if (count && !regionDefs.count(key))
            live.insert(key);

    PressureMetrics metrics;
    metrics.peak = static_cast<unsigned>(live.size());
    recordInterferences(metrics, live);
    for (unsigned index : order) {
        const Node &node = nodes[index];
        for (RegisterKey use : node.uses) {
            if (!isVirtualRegisterKey(use))
                continue;
            auto found = remainingUses.find(use);
            if (found == remainingUses.end() || !found->second)
                continue;
            if (--found->second == 0)
                live.erase(use);
        }
        for (RegisterKey def : node.defs) {
            if (!isVirtualRegisterKey(def))
                continue;
            auto found = remainingUses.find(def);
            if (found != remainingUses.end() && found->second)
                live.insert(def);
        }
        metrics.peak =
            std::max(metrics.peak, static_cast<unsigned>(live.size()));
        metrics.area += live.size();
        recordInterferences(metrics, live);
    }
    return metrics;
}

bool preservesVirtualInstructionOrder(
    const std::vector<Node> &nodes,
    const std::vector<unsigned> &order) {
    std::optional<unsigned> previous;
    for (unsigned index : order) {
        const Node &node = nodes[index];
        bool touchesVirtual = std::any_of(
            node.defs.begin(), node.defs.end(),
            isVirtualRegisterKey) ||
            std::any_of(node.uses.begin(), node.uses.end(),
                        isVirtualRegisterKey);
        if (!touchesVirtual)
            continue;
        if (previous && index < *previous)
            return false;
        previous = index;
    }
    return true;
}

bool scheduleRegion(
    MachineBasicBlock::InstrList &instructions,
    MachineBasicBlock::InstrList::iterator begin,
    MachineBasicBlock::InstrList::iterator end,
    const A53SchedulingModel &model, SchedulingStage stage,
    const std::string &functionName, const std::string &blockName) {
    std::vector<MachineBasicBlock::InstrList::iterator> positions;
    for (auto it = begin; it != end; ++it)
        positions.push_back(it);
    constexpr std::size_t kMinimumRegionSize = 3;
    constexpr std::size_t kMaximumRegionSize = 512;
    if (positions.size() < kMinimumRegionSize ||
        positions.size() > kMaximumRegionSize)
        return false;

    std::vector<Node> nodes(positions.size());
    std::unordered_map<RegisterKey, unsigned> remainingUses;
    std::unordered_set<RegisterKey> live;
    for (unsigned i = 0; i < positions.size(); ++i) {
        const MachineInstr &instruction = *positions[i];
        nodes[i].sched = model.describe(instruction);
        for (const MachineOperand &operand :
             instruction.operands()) {
            if (!operand.isRegister())
                continue;
            RegisterKey key = registerKey(operand);
            if (operand.isDef) {
                nodes[i].defs.insert(key);
            } else {
                nodes[i].uses.insert(key);
                if (isVirtualRegisterKey(key))
                    ++remainingUses[key];
            }
        }
    }
    std::unordered_set<RegisterKey> regionDefs;
    for (const Node &node : nodes)
        for (RegisterKey def : node.defs)
            if (isVirtualRegisterKey(def))
                regionDefs.insert(def);
    for (const auto &[key, count] : remainingUses)
        if (count && !regionDefs.count(key))
            live.insert(key);

    for (unsigned i = 0; i < nodes.size(); ++i) {
        for (unsigned j = i + 1; j < nodes.size(); ++j) {
            unsigned latency = 0;
            bool dependency = false;
            for (RegisterKey def : nodes[i].defs) {
                if (nodes[j].uses.count(def)) {
                    dependency = true;
                    latency = std::max(
                        latency, model.dependencyLatency(
                                     *positions[i], *positions[j],
                                     def));
                }
                if (nodes[j].defs.count(def))
                    dependency = true;
            }
            for (RegisterKey use : nodes[i].uses)
                if (nodes[j].defs.count(use))
                    dependency = true;
            if (memoryDependency(*positions[i], *positions[j]))
                dependency = true;
            if (!dependency)
                continue;
            nodes[i].successors.push_back(Edge{j, latency});
            ++nodes[j].predecessors;
        }
    }

    for (unsigned i = static_cast<unsigned>(nodes.size()); i-- > 0;) {
        nodes[i].height = nodes[i].sched.latency;
        for (const Edge &edge : nodes[i].successors)
            nodes[i].height = std::max(
                nodes[i].height,
                edge.latency + nodes[edge.successor].height);
    }

    std::vector<unsigned> ready;
    for (unsigned i = 0; i < nodes.size(); ++i)
        if (!nodes[i].predecessors)
            ready.push_back(i);

    constexpr unsigned kResourceKinds =
        static_cast<unsigned>(SchedResource::FPMulDiv) + 1;
    std::array<std::vector<unsigned>, kResourceKinds>
        resourceAvailable;
    for (unsigned resource = 0; resource < kResourceKinds;
         ++resource) {
        SchedResource kind = static_cast<SchedResource>(resource);
        resourceAvailable[resource].assign(
            model.resourceCapacity(kind), 0);
    }

    auto resourceUnit = [&](const A53SchedInfo &sched,
                            unsigned cycle) -> std::optional<unsigned> {
        if (sched.resource == SchedResource::None)
            return 0;
        auto &units =
            resourceAvailable[resourceIndex(sched.resource)];
        for (unsigned unit = 0; unit < units.size(); ++unit)
            if (units[unit] <= cycle)
                return unit;
        return std::nullopt;
    };

    std::vector<unsigned> order;
    order.reserve(nodes.size());
    unsigned cycle = 0;
    unsigned issueBudget = A53SchedulingModel::issueWidth();
    while (order.size() != nodes.size()) {
        std::optional<unsigned> bestReadyPosition;
        std::optional<unsigned> bestUnit;
        for (unsigned position = 0; position < ready.size();
             ++position) {
            unsigned index = ready[position];
            const Node &candidate = nodes[index];
            unsigned issueCost = std::min(
                candidate.sched.microOps,
                A53SchedulingModel::issueWidth());
            if (candidate.readyCycle > cycle ||
                issueCost > issueBudget)
                continue;
            auto unit = resourceUnit(candidate.sched, cycle);
            if (!unit)
                continue;
            if (!bestReadyPosition) {
                bestReadyPosition = position;
                bestUnit = unit;
                continue;
            }

            unsigned bestIndex = ready[*bestReadyPosition];
            const Node &best = nodes[bestIndex];
            auto better = [&] {
                if (stage == SchedulingStage::PreRA) {
                    int candidatePressure =
                        registerPressureDelta(
                            candidate, remainingUses, live);
                    int bestPressure =
                        registerPressureDelta(
                            best, remainingUses, live);
                    if (candidatePressure != bestPressure)
                        return candidatePressure < bestPressure;
                }
                if (candidate.height != best.height)
                    return candidate.height > best.height;
                if (candidate.successors.size() !=
                    best.successors.size())
                    return candidate.successors.size() >
                           best.successors.size();
                return index < bestIndex;
            };
            if (better()) {
                bestReadyPosition = position;
                bestUnit = unit;
            }
        }

        if (!bestReadyPosition) {
            ++cycle;
            issueBudget = A53SchedulingModel::issueWidth();
            continue;
        }

        unsigned selected = ready[*bestReadyPosition];
        ready.erase(ready.begin() + *bestReadyPosition);
        Node &node = nodes[selected];
        node.issueCycle = cycle;
        order.push_back(selected);

        unsigned issueCost = std::min(
            node.sched.microOps,
            A53SchedulingModel::issueWidth());
        issueBudget -= issueCost;
        if (node.sched.resource != SchedResource::None) {
            auto &units = resourceAvailable[
                resourceIndex(node.sched.resource)];
            units[*bestUnit] =
                cycle + std::max(1U, node.sched.resourceCycles);
        }

        for (RegisterKey use : node.uses) {
            auto found = remainingUses.find(use);
            if (found == remainingUses.end())
                continue;
            if (found->second)
                --found->second;
            if (!found->second)
                live.erase(use);
        }
        for (RegisterKey def : node.defs)
            if (remainingUses[def])
                live.insert(def);
        for (const Edge &edge : node.successors) {
            Node &successor = nodes[edge.successor];
            successor.readyCycle = std::max(
                successor.readyCycle, cycle + edge.latency);
            if (--successor.predecessors == 0)
                ready.push_back(edge.successor);
        }
        if (!issueBudget) {
            ++cycle;
            issueBudget = A53SchedulingModel::issueWidth();
        }
    }

    unsigned estimatedCycles = 0;
    for (const Node &node : nodes)
        estimatedCycles = std::max(
            estimatedCycles,
            node.issueCycle + node.sched.latency);
    std::vector<unsigned> originalOrder(nodes.size());
    for (unsigned i = 0; i < originalOrder.size(); ++i)
        originalOrder[i] = i;
    PressureMetrics originalPressure =
        virtualRegisterPressure(nodes, originalOrder);
    PressureMetrics scheduledPressure =
        virtualRegisterPressure(nodes, order);
    bool pressureImproved =
        scheduledPressure.peak < originalPressure.peak ||
        (scheduledPressure.peak == originalPressure.peak &&
         scheduledPressure.area < originalPressure.area);
    bool preservesInterferenceGraph = std::includes(
        originalPressure.interferences.begin(),
        originalPressure.interferences.end(),
        scheduledPressure.interferences.begin(),
        scheduledPressure.interferences.end());
    bool preservesVirtualOrder =
        preservesVirtualInstructionOrder(nodes, order);
    bool preRASafe = pressureImproved &&
                     preservesInterferenceGraph &&
                     preservesVirtualOrder;
    if (std::getenv("DEBUG_AARCH64_REWRITE_SCHEDULER")) {
        std::cerr << "[a53-scheduler] " << functionName << ':'
                  << blockName << ' '
                  << (stage == SchedulingStage::PreRA
                          ? "pre-ra"
                          : "post-ra")
                  << " instructions=" << nodes.size()
                  << " cycles=" << estimatedCycles
                  << " pressure=" << originalPressure.peak << '/'
                  << originalPressure.area << "->"
                  << scheduledPressure.peak << '/'
                  << scheduledPressure.area << " interference="
                  << originalPressure.interferences.size() << "->"
                  << scheduledPressure.interferences.size()
                  << " virtual-order="
                  << (preservesVirtualOrder ? "stable" : "changed");
        if (stage == SchedulingStage::PreRA &&
            !preRASafe)
            std::cerr << " rejected";
        std::cerr << '\n';
    }

    bool changed = false;
    for (unsigned i = 0; i < order.size(); ++i)
        changed |= order[i] != i;
    if (!changed)
        return false;
    if (stage == SchedulingStage::PreRA && !preRASafe)
        return false;

    std::list<MachineInstr> scheduled;
    for (unsigned index : order)
        scheduled.splice(scheduled.end(), instructions,
                         positions[index]);
    instructions.splice(end, scheduled);
    return true;
}

} // namespace

A53SchedInfo A53SchedulingModel::describe(
    const MachineInstr &instruction) const {
    Opcode opcode = instruction.opcode();

    if (opcode == Opcode::COPY &&
        !instruction.operands().empty() &&
        isVectorRegisterClass(
            instruction.operands().front().regClass()))
        return A53SchedInfo{1, 6, 1, SchedResource::FPALU};

    if (opcode == Opcode::MOVi32 ||
        opcode == Opcode::MOVi64) {
        unsigned pieces = integerConstantPieces(instruction);
        return A53SchedInfo{
            1, pieces + 2, pieces, SchedResource::ALU};
    }
    if (isIntegerALU(opcode))
        return A53SchedInfo{
            1, isSimpleShift(opcode) ? 2U : 3U, 1,
            SchedResource::ALU};
    if (isIntegerMAC(opcode))
        return A53SchedInfo{1, 4, 1, SchedResource::MAC};
    if (isIntegerDivide(opcode))
        return A53SchedInfo{1, 4, 1, SchedResource::Divide};
    if (isLoad(opcode))
        return A53SchedInfo{
            isPairLoad(opcode) ? 2U : 1U, 4,
            isPairLoad(opcode) ? 2U : 1U,
            SchedResource::LoadStore};
    if (isStore(opcode))
        return A53SchedInfo{1, 4, 1, SchedResource::LoadStore};
    if (isFPALU(opcode))
        return A53SchedInfo{1, 6, 1, SchedResource::FPALU};
    if (isFPMultiply(opcode)) {
        bool accumulate =
            opcode == Opcode::FMLAv4f32 ||
            opcode == Opcode::FMLSv4f32;
        return A53SchedInfo{
            1, accumulate ? 10U : 6U, 1,
            SchedResource::FPMulDiv};
    }
    if (isFPDivide(opcode))
        return A53SchedInfo{
            1, 18, 14, SchedResource::FPMulDiv};

    const InstrDesc &descriptor = InstrInfo::get(opcode);
    return A53SchedInfo{
        1, std::max(1U, descriptor.latency), 1,
        descriptor.resource};
}

unsigned A53SchedulingModel::resourceCapacity(
    SchedResource resource) const {
    if (resource == SchedResource::None)
        return 0;
    return resource == SchedResource::ALU ? 2 : 1;
}

unsigned A53SchedulingModel::dependencyLatency(
    const MachineInstr &producer,
    const MachineInstr &consumer,
    std::uint64_t key) const {
    A53SchedInfo producerInfo = describe(producer);
    unsigned advance = 0;
    if (producerInfo.resource == SchedResource::ALU ||
        producerInfo.resource == SchedResource::MAC ||
        producerInfo.resource == SchedResource::Divide)
        advance = consumerReadAdvance(consumer, key);
    return std::max(
        1U, producerInfo.latency -
                std::min(producerInfo.latency - 1, advance));
}

bool A53MachineScheduler::run(
    MachineFunction &function, SchedulingStage stage) const {
    bool changed = false;
    A53SchedulingModel model;
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        auto regionBegin = instructions.begin();
        for (auto it = instructions.begin();
             it != instructions.end();) {
            if (!schedulingBarrier(*it)) {
                ++it;
                continue;
            }
            changed |= scheduleRegion(
                instructions, regionBegin, it, model, stage,
                function.name(), block->name());
            ++it;
            regionBegin = it;
        }
        changed |= scheduleRegion(
            instructions, regionBegin, instructions.end(),
            model, stage, function.name(), block->name());
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

} // namespace backend::aarch64
