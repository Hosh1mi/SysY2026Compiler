#include "../../../include/mid/hira/analysis/loopInterchangeAnalysis.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"

#include <set>
#include <vector>

namespace hira::polyhedral {
namespace {

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index)
        if (sequence.nodes()[index].get() == node)
            return index;
    return std::nullopt;
}

void collectDefinedValues(
    const HiraSequence &sequence,
    std::set<const HiraValue *> &defined) {
    for (const auto &owner : sequence.nodes()) {
        const HiraNode &node = *owner;
        defined.insert(node.results().begin(),
                       node.results().end());
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(&node)) {
            collectDefinedValues(loop->body(), defined);
        } else if (auto *conditional =
                       dynamic_cast<const HiraIf *>(&node)) {
            collectDefinedValues(
                conditional->thenSequence(), defined);
            collectDefinedValues(
                conditional->elseSequence(), defined);
        }
    }
}

void collectStores(
    const HiraSequence &sequence,
    std::vector<const HiraStore *> &stores) {
    for (const auto &owner : sequence.nodes()) {
        const HiraNode &node = *owner;
        if (auto *store =
                dynamic_cast<const HiraStore *>(&node)) {
            stores.push_back(store);
        } else if (auto *loop =
                       dynamic_cast<const HiraLoop *>(&node)) {
            collectStores(loop->body(), stores);
        } else if (auto *conditional =
                       dynamic_cast<const HiraIf *>(&node)) {
            collectStores(
                conditional->thenSequence(), stores);
            collectStores(
                conditional->elseSequence(), stores);
        }
    }
}

std::optional<MemoryObjectId> accessObject(
    const PolyhedralModel &model, const HiraNode *node,
    MemoryAccessKind kind) {
    const PolyhedralStatement *statement = nullptr;
    for (const PolyhedralStatement &candidate :
         model.statements()) {
        if (candidate.node != node)
            continue;
        if (statement)
            return std::nullopt;
        statement = &candidate;
    }
    if (!statement)
        return std::nullopt;

    std::optional<MemoryObjectId> object;
    for (const AccessRelation &access : model.accesses()) {
        if (access.statement != statement->id ||
            access.kind != kind)
            continue;
        if (object)
            return std::nullopt;
        object = access.object;
    }
    return object;
}

bool safeToRepeat(
    const PolyhedralModel &model,
    const std::vector<const HiraNode *> &nodes,
    const HiraLoop &inner) {
    std::set<const HiraValue *> innerDefinitions;
    collectDefinedValues(inner.body(), innerDefinitions);

    std::vector<const HiraStore *> stores;
    collectStores(inner.body(), stores);
    std::vector<MemoryObjectId> storeObjects;
    storeObjects.reserve(stores.size());
    for (const HiraStore *store : stores) {
        auto object = accessObject(
            model, store, MemoryAccessKind::Write);
        if (!object)
            return false;
        storeObjects.push_back(*object);
    }

    for (const HiraNode *node : nodes) {
        if (!node)
            return false;
        for (const HiraValue *operand : node->operands())
            if (innerDefinitions.count(operand))
                return false;

        if (dynamic_cast<const HiraComputeOp *>(node))
            continue;
        auto *load = dynamic_cast<const HiraLoad *>(node);
        if (!load)
            return false;
        auto loadObject = accessObject(
            model, load, MemoryAccessKind::Read);
        if (!loadObject)
            return false;
        for (MemoryObjectId storeObject : storeObjects)
            if (model.aliasRelation(
                    *loadObject, storeObject) !=
                MemoryAliasKind::NoAlias)
                return false;
    }
    return true;
}

} // namespace

std::optional<AdjacentLoopInterchangePlan>
analyzeAdjacentLoopInterchange(
    const PolyhedralModel &model, HiraLoop &outer,
    HiraLoop &inner) {
    if (!outer.parent() ||
        !analyzeCanonicalLoopControl(outer) ||
        !analyzeCanonicalLoopControl(inner) ||
        inner.body().nodes().size() < 2)
        return std::nullopt;

    const std::size_t payloadEnd =
        outer.body().nodes().size() - 2;
    std::vector<const HiraNode *> repeatedNodes;

    if (inner.parent() == &outer.body()) {
        auto innerPosition =
            nodePosition(outer.body(), &inner);
        if (!innerPosition || *innerPosition >= payloadEnd)
            return std::nullopt;
        for (std::size_t index = 0;
             index < payloadEnd; ++index) {
            const HiraNode *node =
                outer.body().nodes()[index].get();
            if (index != *innerPosition)
                repeatedNodes.push_back(node);
        }
        if (!safeToRepeat(
                model, repeatedNodes, inner))
            return std::nullopt;
        return AdjacentLoopInterchangePlan{
            &outer.body(), *innerPosition};
    }

    HiraIf *guard = nullptr;
    std::size_t guardPosition = 0;
    for (std::size_t index = 0;
         index < payloadEnd; ++index) {
        HiraNode *node =
            outer.body().nodes()[index].get();
        if (auto *candidate =
                dynamic_cast<HiraIf *>(node)) {
            if (guard)
                return std::nullopt;
            guard = candidate;
            guardPosition = index;
        } else {
            repeatedNodes.push_back(node);
        }
    }
    if (!guard || guardPosition + 1 != payloadEnd)
        return std::nullopt;

    HiraSequence *innerSequence = nullptr;
    HiraSequence *otherSequence = nullptr;
    if (inner.parent() == &guard->thenSequence()) {
        innerSequence = &guard->thenSequence();
        otherSequence = &guard->elseSequence();
    } else if (inner.parent() ==
               &guard->elseSequence()) {
        innerSequence = &guard->elseSequence();
        otherSequence = &guard->thenSequence();
    } else {
        return std::nullopt;
    }
    if (!otherSequence->nodes().empty() ||
        innerSequence->nodes().size() != 1 ||
        innerSequence->nodes().front().get() != &inner ||
        !safeToRepeat(model, repeatedNodes, inner))
        return std::nullopt;
    return AdjacentLoopInterchangePlan{
        innerSequence, 0};
}

} // namespace hira::polyhedral
