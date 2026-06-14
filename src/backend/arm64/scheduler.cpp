#include "../../include/backend/arm64/scheduler.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

bool hasIntersection(const std::set<std::string> &a, const std::set<std::string> &b) {
    for (const auto &x : a) {
        if (b.count(x))
            return true;
    }
    return false;
}

bool candidateHasLoadUseHazard(const MachineInstr &prev,
                               const MachineInstr &candidate) {
    return prev.mayLoad && hasIntersection(prev.defs, candidate.uses);
}

bool candidateUsesPrevDef(const MachineInstr &prev,
                          const MachineInstr &candidate) {
    return hasIntersection(prev.defs, candidate.uses);
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
static const std::string kMemoryReg = "$mem";

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
    std::vector<std::set<int>> succ(n);
    std::vector<std::set<int>> pred(n);
    std::map<std::string, int> lastDef;
    std::map<std::string, std::set<int>> liveUsesSinceDef;

    auto addEdge = [&](int from, int to) {
        if (from == to || from < 0 || to < 0)
            return;
        succ[from].insert(to);
        pred[to].insert(from);
    };

    auto processRegUse = [&](int i, const std::string &reg) {
        auto defIt = lastDef.find(reg);
        if (defIt != lastDef.end())
            addEdge(defIt->second, i);
        liveUsesSinceDef[reg].insert(i);
    };

    auto processRegDef = [&](int i, const std::string &reg) {
        // WAW: previous definition → this definition
        auto defIt = lastDef.find(reg);
        if (defIt != lastDef.end())
            addEdge(defIt->second, i);

        // WAR: all outstanding uses must happen before this new definition
        auto useIt = liveUsesSinceDef.find(reg);
        if (useIt != liveUsesSinceDef.end()) {
            for (int user : useIt->second)
                addEdge(user, i);
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

        // Memory operations are modeled conservatively:
        // loads read memory; stores read and define memory. This preserves
        // load/store and store/store order while still allowing load/load
        // reordering inside a scheduling segment.
        if (instrs[i].mayLoad || instrs[i].mayStore)
            processRegUse(i, kMemoryReg);

        // Register defs → WAW + WAR edges
        for (const auto &reg : instrs[i].defs)
            processRegDef(i, reg);

        // Flag def → all prior flag uses must happen before this new setter,
        // and this setter must happen after the previous flag setter (WAW).
        if (instrs[i].setsFlags)
            processRegDef(i, kFlagReg);

        if (instrs[i].mayStore)
            processRegDef(i, kMemoryReg);
    }

    std::vector<int> critical(n, 0);
    std::function<int(int)> computeCritical = [&](int idx) -> int {
        if (critical[idx] > 0)
            return critical[idx];
        int bestSucc = 0;
        for (int s : succ[idx])
            bestSucc = std::max(bestSucc, computeCritical(s));
        critical[idx] = instrs[idx].latency + bestSucc;
        return critical[idx];
    };
    for (int i = 0; i < n; ++i)
        computeCritical(i);

    std::vector<int> remainingPreds(n, 0);
    std::vector<bool> emitted(n, false);
    std::vector<int> ready;
    for (int i = 0; i < n; ++i) {
        remainingPreds[i] = static_cast<int>(pred[i].size());
        if (remainingPreds[i] == 0)
            ready.push_back(i);
    }

    std::vector<int> order;
    order.reserve(n);
    int prev = -1;

    while (!ready.empty()) {
        auto better = [&](int a, int b) {
            if (prev >= 0) {
                bool hazardA = candidateHasLoadUseHazard(instrs[prev], instrs[a]);
                bool hazardB = candidateHasLoadUseHazard(instrs[prev], instrs[b]);
                if (hazardA != hazardB)
                    return !hazardA;
                if (instrs[prev].latency > 1) {
                    bool depA = candidateUsesPrevDef(instrs[prev], instrs[a]);
                    bool depB = candidateUsesPrevDef(instrs[prev], instrs[b]);
                    if (depA != depB)
                        return !depA;
                }
            }
            if (critical[a] != critical[b])
                return critical[a] > critical[b];
            if (instrs[a].latency != instrs[b].latency)
                return instrs[a].latency > instrs[b].latency;
            return instrs[a].originalIndex < instrs[b].originalIndex;
        };

        auto bestIt = ready.begin();
        for (auto it = ready.begin() + 1; it != ready.end(); ++it) {
            if (better(*it, *bestIt))
                bestIt = it;
        }

        int idx = *bestIt;
        ready.erase(bestIt);
        emitted[idx] = true;
        order.push_back(idx);
        prev = idx;

        for (int s : succ[idx]) {
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
