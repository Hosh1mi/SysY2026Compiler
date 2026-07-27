#include "../../../include/mid/hira/transform/loopInvariantCodeMotion.hpp"

#include "../../../include/mid/hira/analysis/loopInvarianceAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <algorithm>
#include <cassert>
#include <set>
#include <vector>

namespace hira {
namespace {

std::size_t nodePosition(const HiraSequence &sequence,
                         const HiraNode *node) {
    const auto &nodes = sequence.nodes();
    auto position = std::find_if(
        nodes.begin(), nodes.end(),
        [node](const std::unique_ptr<HiraNode> &candidate) {
            return candidate.get() == node;
        });
    assert(position != nodes.end() &&
           "Hira node must belong to its recorded parent sequence");
    return static_cast<std::size_t>(
        std::distance(nodes.begin(), position));
}

bool processSequence(
    HiraSequence &sequence,
    const SourceMapping &sourceMapping,
    const ::ArgumentAliasAnalysis *aliasAnalysis);

bool processLoop(
    HiraLoop &loop,
    const SourceMapping &sourceMapping,
    const ::ArgumentAliasAnalysis *aliasAnalysis) {
    bool changed = processSequence(
        loop.body(), sourceMapping, aliasAnalysis);
    HiraSequence *parent = loop.parent();
    if (!parent)
        return changed;

    LoopInvarianceAnalysis analysis(
        loop, &sourceMapping, aliasAnalysis);
    std::set<const HiraNode *> invariantNodes;
    std::vector<HiraNode *> candidates;
    for (const auto &node : loop.body().nodes()) {
        if (dynamic_cast<HiraYield *>(node.get()))
            break;
        if (!analysis.isHoistable(*node, invariantNodes))
            continue;
        invariantNodes.insert(node.get());
        candidates.push_back(node.get());
    }

    for (HiraNode *candidate : candidates) {
        std::unique_ptr<HiraNode> moved = loop.body().remove(candidate);
        assert(moved && "LICM candidate must remain in the loop body");
        std::size_t loopPosition = nodePosition(*parent, &loop);
        parent->insert(loopPosition, std::move(moved));
        changed = true;
    }
    return changed;
}

bool processSequence(
    HiraSequence &sequence,
    const SourceMapping &sourceMapping,
    const ::ArgumentAliasAnalysis *aliasAnalysis) {
    std::vector<HiraNode *> nodes;
    nodes.reserve(sequence.nodes().size());
    for (const auto &node : sequence.nodes())
        nodes.push_back(node.get());

    bool changed = false;
    for (HiraNode *node : nodes) {
        if (auto *loop = dynamic_cast<HiraLoop *>(node)) {
            changed |= processLoop(
                *loop, sourceMapping, aliasAnalysis);
            continue;
        }
        if (auto *condition = dynamic_cast<HiraIf *>(node)) {
            changed |= processSequence(
                condition->thenSequence(), sourceMapping,
                aliasAnalysis);
            changed |= processSequence(
                condition->elseSequence(), sourceMapping,
                aliasAnalysis);
        }
    }
    return changed;
}

} // namespace

bool hoistLoopInvariants(
    HiraRegion &region,
    const ::ArgumentAliasAnalysis *aliasAnalysis) {
    bool changed = processSequence(
        region.rootSequence(), region.sourceMapping(),
        aliasAnalysis);
    if (changed)
        region.markModified();
    return changed;
}

} // namespace hira
