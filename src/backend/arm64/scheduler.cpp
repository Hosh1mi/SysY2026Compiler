#include "../../include/backend/arm64/scheduler.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

bool memoryRangesOverlap(const MachineInstr &a, const MachineInstr &b) {
    if (!a.memOffsetKnown || !b.memOffsetKnown)
        return true;
    if (a.memBase != b.memBase)
        return true;
    if (a.memWidth <= 0 || b.memWidth <= 0)
        return true;

    int aEnd = a.memOffset + a.memWidth;
    int bEnd = b.memOffset + b.memWidth;
    return a.memOffset < bEnd && b.memOffset < aEnd;
}

bool memoryOrderNeeded(const MachineInstr &prev, const MachineInstr &cur) {
    if ((!prev.mayLoad && !prev.mayStore) || (!cur.mayLoad && !cur.mayStore))
        return false;
    if (!prev.mayStore && !cur.mayStore)
        return false;
    return memoryRangesOverlap(prev, cur);
}

} // namespace

void MachineScheduler::schedule(MachineFunction &func) const {
    for (auto &block : func.blocks) {
        std::vector<MachineInstr> scheduled;
        std::vector<MachineInstr> segment;

        auto flushSegment = [&](bool preserveFlagLiveOut = false) {
            if (segment.empty())
                return;
            auto reordered = scheduleSegment(segment, preserveFlagLiveOut);
            scheduled.insert(scheduled.end(),
                             std::make_move_iterator(reordered.begin()),
                             std::make_move_iterator(reordered.end()));
            segment.clear();
        };

        for (auto &inst : block.instrs) {
            if (inst.isBarrier || inst.isLabelLike) {
                flushSegment(inst.usesFlags);
                scheduled.push_back(std::move(inst));
            } else {
                segment.push_back(std::move(inst));
            }
        }

        flushSegment();
        block.instrs = std::move(scheduled);
    }
}

// Virtual register name used to track flag (NZCV) dependencies in the
// dependency graph.  Instructions that set flags are treated as defining
// $flags; instructions that use flags are treated as using $flags.  This
// prevents the scheduler from reordering flag users before flag setters.
static const std::string kFlagReg = "$flags";

std::vector<MachineInstr> MachineScheduler::scheduleSegment(const std::vector<MachineInstr> &segment,
                                                            bool preserveFlagLiveOut) const {
    if (segment.size() <= 1)
        return segment;

    std::vector<MachineInstr> instrs = segment;
    int flagLiveOutIndex = -1;
    if (preserveFlagLiveOut) {
        MachineInstr liveOut;
        liveOut.usesFlags = true;
        liveOut.originalIndex = instrs.back().originalIndex + 1;
        flagLiveOutIndex = static_cast<int>(instrs.size());
        instrs.push_back(std::move(liveOut));
    }
    const int n = static_cast<int>(instrs.size());
    std::vector<std::map<int, int>> succ(n);
    std::vector<std::map<int, int>> pred(n);
    std::map<std::string, int> lastDef;
    std::map<std::string, std::set<int>> liveUsesSinceDef;
    std::vector<int> previousMemoryOps;

    auto addEdge = [&](int from, int to, int latency) {
        if (from == to || from < 0 || to < 0)
            return;
        latency = std::max(0, latency);
        auto succIt = succ[from].find(to);
        if (succIt == succ[from].end() || latency > succIt->second)
            succ[from][to] = latency;
        auto predIt = pred[to].find(from);
        if (predIt == pred[to].end() || latency > predIt->second)
            pred[to][from] = latency;
    };

    auto processRegUse = [&](int i, const std::string &reg) {
        auto defIt = lastDef.find(reg);
        if (defIt != lastDef.end())
            addEdge(defIt->second, i, instrs[defIt->second].latency);
        liveUsesSinceDef[reg].insert(i);
    };

    auto processRegDef = [&](int i, const std::string &reg) {
        // WAW: previous definition → this definition
        auto defIt = lastDef.find(reg);
        if (defIt != lastDef.end())
            addEdge(defIt->second, i, 0);

        // WAR: all outstanding uses must happen before this new definition
        auto useIt = liveUsesSinceDef.find(reg);
        if (useIt != liveUsesSinceDef.end()) {
            for (int user : useIt->second)
                addEdge(user, i, 0);
            useIt->second.clear();
        }

        lastDef[reg] = i;
    };

    for (int i = 0; i < n; ++i) {
        // Register uses → RAW edges from last def to this use
        for (const auto &reg : instrs[i].uses)
            processRegUse(i, reg);

        // Flag uses → must happen after the most recent flag setter
        if (instrs[i].usesFlags)
            processRegUse(i, kFlagReg);

        // Preserve memory order only for pairs that can alias and where at
        // least one side writes.  Unknown addresses alias conservatively;
        // fixed, non-overlapping frame slots may move independently.
        if (instrs[i].mayLoad || instrs[i].mayStore) {
            for (int prev : previousMemoryOps) {
                if (memoryOrderNeeded(instrs[prev], instrs[i]))
                    addEdge(prev, i, 0);
            }
            previousMemoryOps.push_back(i);
        }

        // Register defs → WAW + WAR edges
        for (const auto &reg : instrs[i].defs)
            processRegDef(i, reg);

        // Flag def → all prior flag uses must happen before this new setter,
        // and this setter must happen after the previous flag setter (WAW).
        if (instrs[i].setsFlags)
            processRegDef(i, kFlagReg);

    }

    std::vector<int> critical(n, 0);
    std::function<int(int)> computeCritical = [&](int idx) -> int {
        if (critical[idx] > 0)
            return critical[idx];
        int best = instrs[idx].latency;
        for (const auto &edge : succ[idx])
            best = std::max(best, edge.second + computeCritical(edge.first));
        critical[idx] = std::max(1, best);
        return critical[idx];
    };
    for (int i = 0; i < n; ++i)
        computeCritical(i);

    std::vector<int> remainingPreds(n, 0);
    std::vector<int> readyCycle(n, 0);
    std::vector<int> emitCycle(n, -1);
    std::vector<int> ready;
    for (int i = 0; i < n; ++i) {
        remainingPreds[i] = static_cast<int>(pred[i].size());
        if (remainingPreds[i] == 0)
            ready.push_back(i);
    }

    std::vector<int> order;
    order.reserve(n);
    int cycle = 0;

    while (!ready.empty()) {
        auto available = [&]() {
            std::vector<int> result;
            for (int idx : ready) {
                if (readyCycle[idx] <= cycle)
                    result.push_back(idx);
            }
            return result;
        };

        auto choices = available();
        if (choices.empty()) {
            int nextCycle = std::numeric_limits<int>::max();
            for (int idx : ready)
                nextCycle = std::min(nextCycle, readyCycle[idx]);
            if (nextCycle == std::numeric_limits<int>::max())
                return segment;
            cycle = std::max(cycle, nextCycle);
            continue;
        }

        auto better = [&](int a, int b) {
            bool pseudoA = (a == flagLiveOutIndex);
            bool pseudoB = (b == flagLiveOutIndex);
            if (pseudoA != pseudoB)
                return !pseudoA;
            if (critical[a] != critical[b])
                return critical[a] > critical[b];
            if (instrs[a].latency != instrs[b].latency)
                return instrs[a].latency > instrs[b].latency;
            return instrs[a].originalIndex < instrs[b].originalIndex;
        };

        int idx = choices.front();
        for (size_t i = 1; i < choices.size(); ++i) {
            if (better(choices[i], idx))
                idx = choices[i];
        }

        auto readyIt = std::find(ready.begin(), ready.end(), idx);
        if (readyIt == ready.end())
            return segment;
        ready.erase(readyIt);
        emitCycle[idx] = cycle;
        order.push_back(idx);
        if (idx != flagLiveOutIndex)
            ++cycle;

        for (const auto &edge : succ[idx]) {
            int s = edge.first;
            readyCycle[s] = std::max(readyCycle[s], emitCycle[idx] + edge.second);
            if (--remainingPreds[s] == 0)
                ready.push_back(s);
        }
    }

    if (static_cast<int>(order.size()) != n) {
        return segment;
    }

    std::vector<MachineInstr> out;
    out.reserve(order.size());
    for (int idx : order) {
        if (idx == flagLiveOutIndex)
            continue;
        out.push_back(std::move(instrs[idx]));
    }
    return out;
}
