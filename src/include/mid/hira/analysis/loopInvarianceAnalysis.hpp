#pragma once

#include <set>

namespace hira {

class HiraLoop;
class HiraNode;
class HiraSequence;
class HiraValue;

class LoopInvarianceAnalysis {
public:
    explicit LoopInvarianceAnalysis(const HiraLoop &loop);

    bool isInvariant(
        const HiraValue *value,
        const std::set<const HiraNode *> &assumedInvariant = {}) const;
    bool isHoistable(
        const HiraNode &node,
        const std::set<const HiraNode *> &assumedInvariant = {}) const;

private:
    void collectSequence(const HiraSequence &sequence);
    void collectNode(const HiraNode &node);

    const HiraLoop &loop_;
    std::set<const HiraNode *> internalNodes_;
};

} // namespace hira
