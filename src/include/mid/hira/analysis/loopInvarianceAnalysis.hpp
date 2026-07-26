#pragma once

#include <set>

class ArgumentAliasAnalysis;

namespace hira {

class SourceMapping;
class HiraLoop;
class HiraNode;
class HiraSequence;
class HiraValue;

class LoopInvarianceAnalysis {
public:
    LoopInvarianceAnalysis(
        const HiraLoop &loop,
        const SourceMapping *sourceMapping = nullptr,
        const ::ArgumentAliasAnalysis *aliasAnalysis = nullptr);

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
    const SourceMapping *sourceMapping_ = nullptr;
    const ::ArgumentAliasAnalysis *aliasAnalysis_ = nullptr;
    std::set<const HiraNode *> internalNodes_;
    std::set<const HiraValue *> storedBases_;
};

} // namespace hira
