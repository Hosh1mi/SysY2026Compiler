#include "backend/scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <list>
#include <set>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

using RegisterKey = std::uint64_t;

RegisterKey registerKey(const MachineOperand &operand) {
    if (operand.isPhysicalRegister())
        return (1ULL << 63) |
               static_cast<unsigned>(operand.physicalRegister());
    return operand.virtualRegister();
}

RegisterKey registerKey(PhysReg reg) {
    return (1ULL << 63) | static_cast<unsigned>(reg);
}

struct Node {
    std::set<RegisterKey> defs;
    std::set<RegisterKey> uses;
    std::vector<unsigned> successors;
    unsigned predecessors = 0;
    unsigned height = 0;
    unsigned latency = 1;
    SchedResource resource = SchedResource::None;
    bool load = false;
    bool store = false;
};

bool intersects(const std::set<RegisterKey> &lhs,
                const std::set<RegisterKey> &rhs) {
    auto a = lhs.begin();
    auto b = rhs.begin();
    while (a != lhs.end() && b != rhs.end()) {
        if (*a == *b)
            return true;
        if (*a < *b)
            ++a;
        else
            ++b;
    }
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

bool scheduleRegion(
    MachineBasicBlock::InstrList &instructions,
    MachineBasicBlock::InstrList::iterator begin,
    MachineBasicBlock::InstrList::iterator end) {
    std::vector<MachineBasicBlock::InstrList::iterator> positions;
    for (auto it = begin; it != end; ++it)
        positions.push_back(it);
    constexpr std::size_t kMinimumRegionSize = 3;
    constexpr std::size_t kMaximumRegionSize = 512;
    if (positions.size() < kMinimumRegionSize ||
        positions.size() > kMaximumRegionSize)
        return false;

    std::vector<Node> nodes(positions.size());
    for (unsigned i = 0; i < positions.size(); ++i) {
        const MachineInstr &instruction = *positions[i];
        const InstrDesc &descriptor =
            InstrInfo::get(instruction.opcode());
        nodes[i].latency = std::max(1U, descriptor.latency);
        nodes[i].resource = descriptor.resource;
        nodes[i].load = instruction.mayLoad();
        nodes[i].store = instruction.mayStore();
        for (const MachineOperand &operand :
             instruction.operands()) {
            if (!operand.isRegister())
                continue;
            (operand.isDef ? nodes[i].defs : nodes[i].uses)
                .insert(registerKey(operand));
        }
        if (instruction.readsRegister(PhysReg::NZCV))
            nodes[i].uses.insert(registerKey(PhysReg::NZCV));
        if (instruction.definesRegister(PhysReg::NZCV))
            nodes[i].defs.insert(registerKey(PhysReg::NZCV));
    }

    for (unsigned i = 0; i < nodes.size(); ++i) {
        for (unsigned j = i + 1; j < nodes.size(); ++j) {
            bool registerDependency =
                intersects(nodes[i].defs, nodes[j].uses) ||
                intersects(nodes[i].uses, nodes[j].defs) ||
                intersects(nodes[i].defs, nodes[j].defs);
            bool memoryDependency =
                (nodes[i].load || nodes[i].store) &&
                (nodes[j].load || nodes[j].store) &&
                (nodes[i].store || nodes[j].store);
            if (!registerDependency && !memoryDependency)
                continue;
            nodes[i].successors.push_back(j);
            ++nodes[j].predecessors;
        }
    }

    for (unsigned i = static_cast<unsigned>(nodes.size()); i-- > 0;) {
        unsigned successorHeight = 0;
        for (unsigned successor : nodes[i].successors)
            successorHeight =
                std::max(successorHeight, nodes[successor].height);
        nodes[i].height = nodes[i].latency + successorHeight;
    }

    std::vector<unsigned> ready;
    for (unsigned i = 0; i < nodes.size(); ++i)
        if (!nodes[i].predecessors)
            ready.push_back(i);
    std::vector<unsigned> order;
    order.reserve(nodes.size());
    SchedResource previousResource = SchedResource::None;
    while (!ready.empty()) {
        auto best = std::max_element(
            ready.begin(), ready.end(), [&](unsigned lhs, unsigned rhs) {
                auto score = [&](unsigned index) {
                    long value =
                        static_cast<long>(nodes[index].height) * 16;
                    if (nodes[index].load)
                        value += 8;
                    if (nodes[index].resource != previousResource)
                        value += 2;
                    value -= static_cast<long>(index) / 64;
                    return value;
                };
                long lhsScore = score(lhs);
                long rhsScore = score(rhs);
                return lhsScore != rhsScore ? lhsScore < rhsScore
                                            : lhs > rhs;
            });
        unsigned selected = *best;
        ready.erase(best);
        order.push_back(selected);
        previousResource = nodes[selected].resource;
        for (unsigned successor : nodes[selected].successors)
            if (--nodes[successor].predecessors == 0)
                ready.push_back(successor);
    }
    if (order.size() != nodes.size())
        return false;
    bool changed = false;
    for (unsigned i = 0; i < order.size(); ++i)
        changed |= order[i] != i;
    if (!changed)
        return false;

    std::list<MachineInstr> scheduled;
    for (unsigned index : order)
        scheduled.splice(scheduled.end(), instructions,
                         positions[index]);
    instructions.splice(end, scheduled);
    return true;
}

} // namespace

bool A53MachineScheduler::run(MachineFunction &function) const {
    bool changed = false;
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        auto regionBegin = instructions.begin();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (!schedulingBarrier(*it)) {
                ++it;
                continue;
            }
            changed |= scheduleRegion(instructions, regionBegin, it);
            ++it;
            regionBegin = it;
        }
        changed |= scheduleRegion(
            instructions, regionBegin, instructions.end());
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

} // namespace backend::aarch64
