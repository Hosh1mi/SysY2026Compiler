#include "../../include/backend/arm64/scheduler.hpp"

#include "../../include/backend/arm64/machine.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
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

} // namespace

std::string MachineScheduler::scheduleFunctionText(const std::string &asmText) const {
    std::istringstream input(asmText);
    std::ostringstream output;
    std::vector<std::string> segment;
    std::string line;
    int lineIndex = 0;
    int segmentStart = 0;

    auto flushSegment = [&]() {
        if (segment.empty())
            return;
        output << scheduleSegment(segment, segmentStart);
        segment.clear();
    };

    while (std::getline(input, line)) {
        MachineInstr mi = parseMachineInstr(line, lineIndex);
        if (mi.isBarrier || mi.isLabelLike) {
            flushSegment();
            output << line << "\n";
        } else {
            if (segment.empty())
                segmentStart = lineIndex;
            segment.push_back(line);
        }
        ++lineIndex;
    }
    flushSegment();

    return output.str();
}

std::string MachineScheduler::scheduleSegment(const std::vector<std::string> &segment,
                                               int firstOriginalIndex) const {
    if (segment.size() <= 1) {
        std::ostringstream out;
        for (const auto &line : segment)
            out << line << "\n";
        return out.str();
    }

    std::vector<MachineInstr> instrs;
    instrs.reserve(segment.size());
    for (size_t i = 0; i < segment.size(); ++i)
        instrs.push_back(parseMachineInstr(segment[i], firstOriginalIndex + static_cast<int>(i)));

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

    for (int i = 0; i < n; ++i) {
        for (const auto &reg : instrs[i].uses) {
            auto defIt = lastDef.find(reg);
            if (defIt != lastDef.end())
                addEdge(defIt->second, i);
            liveUsesSinceDef[reg].insert(i);
        }

        for (const auto &reg : instrs[i].defs) {
            auto defIt = lastDef.find(reg);
            if (defIt != lastDef.end())
                addEdge(defIt->second, i);

            auto useIt = liveUsesSinceDef.find(reg);
            if (useIt != liveUsesSinceDef.end()) {
                for (int user : useIt->second)
                    addEdge(user, i);
                useIt->second.clear();
            }

            lastDef[reg] = i;
        }
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
        std::ostringstream fallback;
        for (const auto &line : segment)
            fallback << line << "\n";
        return fallback.str();
    }

    std::ostringstream out;
    for (int idx : order)
        out << instrs[idx].text << "\n";
    return out.str();
}
