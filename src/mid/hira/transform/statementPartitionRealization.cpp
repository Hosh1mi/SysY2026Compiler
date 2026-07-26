#include "../../../include/mid/hira/transform/statementPartitionRealization.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

StatementPartitionRealizationResult reject(
    StatementPartitionRealizationError error,
    std::string detail) {
    StatementPartitionRealizationResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index)
        if (sequence.nodes()[index].get() == node)
            return index;
    return std::nullopt;
}

HiraValue *mappedValue(
    HiraValue *value, const HiraLoop &sourceLoop,
    const std::map<HiraValue *, HiraValue *> &values) {
    auto mapped = values.find(value);
    if (mapped != values.end())
        return mapped->second;
    HiraNode *definition = value ? value->definingNode() : nullptr;
    if (definition && definition->parent() ==
                          &sourceLoop.body())
        return nullptr;
    return value;
}

std::unique_ptr<HiraNode> cloneOrdinaryNode(
    HiraRegion &region, const HiraLoop &sourceLoop,
    HiraNode &node,
    std::map<HiraValue *, HiraValue *> &values) {
    std::unique_ptr<HiraNode> clone;
    if (auto *compute =
            dynamic_cast<HiraComputeOp *>(&node)) {
        clone = std::make_unique<HiraComputeOp>(
            compute->computeKind(), compute->predicate());
    } else if (auto *load =
                   dynamic_cast<HiraLoad *>(&node)) {
        HiraValue *address =
            mappedValue(load->address(), sourceLoop, values);
        if (!address)
            return nullptr;
        clone = std::make_unique<HiraLoad>(address);
    } else if (auto *store =
                   dynamic_cast<HiraStore *>(&node)) {
        HiraValue *value =
            mappedValue(store->value(), sourceLoop, values);
        HiraValue *address =
            mappedValue(store->address(), sourceLoop, values);
        if (!value || !address)
            return nullptr;
        clone = std::make_unique<HiraStore>(value, address);
    } else {
        return nullptr;
    }

    if (dynamic_cast<HiraComputeOp *>(&node))
        for (HiraValue *operand : node.operands()) {
            HiraValue *mapped =
                mappedValue(operand, sourceLoop, values);
            if (!mapped)
                return nullptr;
            clone->addOperand(mapped);
        }

    for (HiraValue *result : node.results()) {
        HiraValue *newResult =
            region.createValue(result->type());
        clone->addResult(newResult);
        values[result] = newResult;
    }
    return clone;
}

void appendLoopControl(HiraRegion &region, HiraLoop &loop) {
    HiraValue *next =
        region.createValue(loop.induction()->type());
    auto update =
        std::make_unique<HiraComputeOp>(ComputeKind::Add);
    update->addOperand(loop.induction());
    update->addOperand(loop.step());
    update->addResult(next);
    loop.body().append(std::move(update));
    loop.addYieldValue(next);
    auto yield = std::make_unique<HiraYield>();
    yield->addOperand(next);
    loop.body().append(std::move(yield));
}

bool partitionIsSelfContained(
    const HiraLoop &loop,
    const std::vector<HiraNode *> &nodes) {
    std::set<const HiraValue *> available{
        loop.induction(),
    };
    for (HiraNode *node : nodes) {
        if (!dynamic_cast<HiraComputeOp *>(node) &&
            !dynamic_cast<HiraLoad *>(node) &&
            !dynamic_cast<HiraStore *>(node))
            return false;
        for (HiraValue *operand : node->operands()) {
            HiraNode *definition =
                operand ? operand->definingNode() : nullptr;
            if (definition &&
                definition->parent() == &loop.body() &&
                !available.count(operand))
                return false;
        }
        for (HiraValue *result : node->results())
            available.insert(result);
    }
    return true;
}

} // namespace

StatementPartitionRealizationResult realizeStatementPartitions(
    HiraRegion &region, const PolyhedralModel &model,
    const StatementPartitionResult &partitions) {
    if (partitions.kind() !=
            StatementPartitionKind::Distributable ||
        partitions.partitions().size() < 2)
        return reject(StatementPartitionRealizationError::Indivisible,
                      "no-distribution-plan");
    const auto &firstPartition =
        partitions.partitions().front();
    if (firstPartition.dimensions.size() != 1)
        return reject(
            StatementPartitionRealizationError::UnsupportedDomain,
            "only-single-loop-distribution-is-realizable");

    AffineVariable dimension =
        firstPartition.dimensions.front();
    const IterationDomain *domain = nullptr;
    for (const IterationDomain &candidate :
         model.domains())
        if (candidate.dimension == dimension) {
            domain = &candidate;
            break;
        }
    if (!domain || !domain->loop ||
        domain->dimensions.size() != 1)
        return reject(
            StatementPartitionRealizationError::UnsupportedDomain,
            "missing-root-loop-domain");
    auto *sourceLoop =
        const_cast<HiraLoop *>(domain->loop);
    if (!sourceLoop->carriedValues().empty())
        return reject(
            StatementPartitionRealizationError::LoopCarriedState,
            "loop-carried-state");
    HiraSequence *parent = sourceLoop->parent();
    auto sourcePosition =
        parent ? nodePosition(*parent, sourceLoop)
               : std::nullopt;
    if (!parent || !sourcePosition ||
        sourceLoop->body().nodes().size() < 2)
        return reject(
            StatementPartitionRealizationError::InvalidLoopBody,
            "invalid-loop-owner");

    std::map<const HiraNode *, StatementPartitionId>
        nodePartitions;
    for (const PolyhedralStatement &statement :
         model.statements()) {
        if (!statement.node ||
            statement.id >=
                partitions.partitionByStatement().size())
            return reject(
                StatementPartitionRealizationError::InvalidLoopBody,
                "invalid-statement-partition");
        nodePartitions[statement.node] =
            partitions.partitionByStatement()[
                statement.id];
    }

    const std::size_t payloadCount =
        sourceLoop->body().nodes().size() - 2;
    std::vector<HiraNode *> payload;
    payload.reserve(payloadCount);
    for (std::size_t index = 0;
         index < payloadCount; ++index) {
        HiraNode *node =
            sourceLoop->body().nodes()[index].get();
        if (!nodePartitions.count(node))
            return reject(
                StatementPartitionRealizationError::InvalidLoopBody,
                "unmodelled-loop-node");
        payload.push_back(node);
    }

    std::vector<std::vector<HiraNode *>> nodesByPartition(
        partitions.partitions().size());
    for (HiraNode *node : payload)
        nodesByPartition[nodePartitions[node]].push_back(node);
    for (const auto &nodes : nodesByPartition)
        if (!partitionIsSelfContained(*sourceLoop, nodes))
            return reject(
                StatementPartitionRealizationError::
                    CrossPartitionScalar,
                "partition-is-not-self-contained");

    std::size_t insertionPosition = *sourcePosition + 1;
    for (std::size_t partitionIndex = 1;
         partitionIndex < nodesByPartition.size();
         ++partitionIndex) {
        HiraValue *induction = region.createValue(
            sourceLoop->induction()->type());
        auto loopOwner = std::make_unique<HiraLoop>(
            induction, sourceLoop->lowerBound(),
            sourceLoop->upperBound(), sourceLoop->step());
        HiraLoop *distributedLoop = loopOwner.get();
        if (Loop *mappedSource =
                region.sourceMapping().sourceLoop(sourceLoop))
            region.sourceMapping().mapLoop(
                distributedLoop, mappedSource);

        std::map<HiraValue *, HiraValue *> values;
        values[sourceLoop->induction()] = induction;
        for (HiraNode *node :
             nodesByPartition[partitionIndex]) {
            std::unique_ptr<HiraNode> clone =
                cloneOrdinaryNode(
                    region, *sourceLoop, *node, values);
            if (!clone)
                return reject(
                    StatementPartitionRealizationError::
                        CrossPartitionScalar,
                    "unavailable-cloned-operand");
            if (Instruction *source =
                    region.sourceMapping()
                        .sourceInstruction(node))
                region.sourceMapping().mapNode(
                    clone.get(), source);
            distributedLoop->body().append(
                std::move(clone));
        }
        appendLoopControl(region, *distributedLoop);
        parent->insert(insertionPosition++,
                       std::move(loopOwner));
    }

    std::set<HiraNode *> retained(
        nodesByPartition.front().begin(),
        nodesByPartition.front().end());
    for (HiraNode *node : payload)
        if (!retained.count(node))
            sourceLoop->body().remove(node);

    region.markModified();
    return {
        true, StatementPartitionRealizationError::None, {}};
}

const char *statementPartitionRealizationErrorName(
    StatementPartitionRealizationError error) {
    switch (error) {
    case StatementPartitionRealizationError::None:
        return "none";
    case StatementPartitionRealizationError::Indivisible:
        return "indivisible";
    case StatementPartitionRealizationError::UnsupportedDomain:
        return "unsupported-domain";
    case StatementPartitionRealizationError::LoopCarriedState:
        return "loop-carried-state";
    case StatementPartitionRealizationError::InvalidLoopBody:
        return "invalid-loop-body";
    case StatementPartitionRealizationError::CrossPartitionScalar:
        return "cross-partition-scalar";
    case StatementPartitionRealizationError::UnsupportedNode:
        return "unsupported-node";
    }
    return "unknown";
}

} // namespace hira::polyhedral
