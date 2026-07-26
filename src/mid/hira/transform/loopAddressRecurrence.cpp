#include "../../../include/mid/hira/transform/loopAddressRecurrence.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace hira::polyhedral {
namespace {

const IterationDomain *findDomain(
    const PolyhedralModel &model, AffineVariable dimension) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimension == dimension)
            return &domain;
    return nullptr;
}

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index)
        if (sequence.nodes()[index].get() == node)
            return index;
    return std::nullopt;
}

void collectNodes(
    const HiraSequence &sequence,
    std::set<const HiraNode *> &nodes) {
    for (const auto &owner : sequence.nodes()) {
        const HiraNode *node = owner.get();
        nodes.insert(node);
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(node))
            collectNodes(loop->body(), nodes);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(node)) {
            collectNodes(condition->thenSequence(), nodes);
            collectNodes(condition->elseSequence(), nodes);
        }
    }
}

bool canMaterialize(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::GetElementPtr:
    case ComputeKind::ZExt:
    case ComputeKind::BitCast:
        return true;
    default:
        return false;
    }
}

HiraValue *materializeAtEntry(
    HiraRegion &region, HiraLoop &loop,
    const std::set<const HiraNode *> &internal,
    HiraSequence &parent, std::size_t &position,
    const HiraValue *source,
    std::map<const HiraValue *, HiraValue *> &cache) {
    if (!source)
        return nullptr;
    auto cached = cache.find(source);
    if (cached != cache.end())
        return cached->second;
    if (source == loop.induction())
        return cache[source] = loop.lowerBound();

    const HiraNode *definition = source->definingNode();
    if (!definition || !internal.count(definition))
        return cache[source] =
                   const_cast<HiraValue *>(source);
    auto *compute =
        dynamic_cast<const HiraComputeOp *>(definition);
    if (!compute || compute->results().size() != 1 ||
        compute->results().front() != source ||
        !canMaterialize(compute->computeKind()))
        return nullptr;

    auto owner = std::make_unique<HiraComputeOp>(
        compute->computeKind(), compute->predicate());
    for (const HiraValue *operand : compute->operands()) {
        HiraValue *materialized = materializeAtEntry(
            region, loop, internal, parent, position,
            operand, cache);
        if (!materialized)
            return nullptr;
        owner->addOperand(materialized);
    }
    HiraValue *result = region.createValue(source->type());
    owner->addResult(result);
    parent.insert(position++, std::move(owner));
    cache[source] = result;
    return result;
}

bool addressHasSupportedUses(
    const HiraSequence &sequence, const HiraValue *address) {
    for (const auto &owner : sequence.nodes()) {
        const HiraNode *node = owner.get();
        if (auto *condition =
                dynamic_cast<const HiraIf *>(node)) {
            for (const HiraIf::ResultBinding &binding :
                 condition->resultBindings())
                if (binding.thenValue == address ||
                    binding.elseValue == address)
                    return false;
            if (!addressHasSupportedUses(
                    condition->thenSequence(), address) ||
                !addressHasSupportedUses(
                    condition->elseSequence(), address))
                return false;
        } else if (auto *nested =
                       dynamic_cast<const HiraLoop *>(node)) {
            for (const HiraLoop::CarriedBinding &binding :
                 nested->carriedValues())
                if (binding.initial == address ||
                    binding.yielded == address)
                    return false;
            if (!addressHasSupportedUses(
                    nested->body(), address))
                return false;
        }
    }
    return true;
}

void replaceAddressUses(
    HiraSequence &sequence, HiraValue *address,
    HiraValue *replacement) {
    for (const auto &owner : sequence.nodes()) {
        HiraNode *node = owner.get();
        for (std::size_t index = 0;
             index < node->operands().size(); ++index)
            if (node->operands()[index] == address)
                node->replaceOperand(index, replacement);
        if (auto *condition = dynamic_cast<HiraIf *>(node)) {
            replaceAddressUses(
                condition->thenSequence(), address,
                replacement);
            replaceAddressUses(
                condition->elseSequence(), address,
                replacement);
        } else if (auto *nested =
                       dynamic_cast<HiraLoop *>(node)) {
            replaceAddressUses(
                nested->body(), address, replacement);
        }
    }
}

struct Candidate {
    HiraValue *address = nullptr;
    HiraComputeOp *definition = nullptr;
    IntegerType *offsetType = nullptr;
    std::int64_t elementStride = 0;
};

} // namespace

LoopAddressRecurrenceResult introduceLoopAddressRecurrences(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension) {
    LoopAddressRecurrenceResult result;
    const IterationDomain *domain =
        findDomain(model, dimension);
    HiraLoop *loop =
        domain ? const_cast<HiraLoop *>(domain->loop) : nullptr;
    HiraSequence *parent = loop ? loop->parent() : nullptr;
    auto loopPosition =
        parent ? nodePosition(*parent, loop) : std::nullopt;
    if (!loop || !parent || !loopPosition ||
        loop->role() != HiraLoop::Role::Ordinary) {
        result.detail = "unsupported-loop";
        return result;
    }

    std::map<HiraValue *, std::int64_t> strides;
    std::set<HiraValue *> ambiguous;
    // Pointer-keyed maps are lookup tables only.  Preserve model access order
    // for IR construction so allocator addresses cannot affect emitted code.
    std::vector<HiraValue *> addressOrder;
    for (const AccessRelation &access : model.accesses()) {
        if (access.statement >= model.statements().size())
            continue;
        const PolyhedralStatement &statement =
            model.statements()[access.statement];
        if (std::find(
                statement.dimensions.begin(),
                statement.dimensions.end(), dimension) ==
            statement.dimensions.end())
            continue;
        HiraValue *address = nullptr;
        if (auto *load =
                dynamic_cast<const HiraLoad *>(statement.node))
            address = load->address();
        else if (auto *store =
                     dynamic_cast<const HiraStore *>(statement.node))
            address = store->address();
        if (!address)
            continue;
        auto byteStride =
            analyzeLinearAccessStride(model, access, dimension);
        auto elementSize =
            analyzeAccessElementSize(model, access);
        if (!byteStride || !elementSize || !*byteStride ||
            !*elementSize || *byteStride % *elementSize)
            continue;
        const std::int64_t elementStride =
            *byteStride / *elementSize;
        auto inserted = strides.emplace(address, elementStride);
        if (inserted.second)
            addressOrder.push_back(address);
        else if (inserted.first->second != elementStride)
            ambiguous.insert(address);
    }

    std::vector<Candidate> candidates;
    for (HiraValue *address : addressOrder) {
        if (ambiguous.count(address))
            continue;
        auto stride = strides.find(address);
        if (stride == strides.end())
            continue;
        auto *definition = dynamic_cast<HiraComputeOp *>(
            address->definingNode());
        if (!definition ||
            definition->computeKind() !=
                ComputeKind::GetElementPtr ||
            definition->parent() != &loop->body() ||
            definition->operands().size() < 2 ||
            definition->results().size() != 1 ||
            !addressHasSupportedUses(loop->body(), address))
            continue;
        auto *offsetType = dynamic_cast<IntegerType *>(
            definition->operands().back()->type());
        if (!offsetType)
            continue;
        candidates.push_back(
            {address, definition, offsetType, stride->second});
    }
    if (candidates.empty()) {
        result.detail = "no-affine-point-address";
        return result;
    }

    std::set<const HiraNode *> internal;
    collectNodes(loop->body(), internal);
    std::size_t insertion = *loopPosition;
    std::map<const HiraValue *, HiraValue *> entryValues;
    struct Realized {
        Candidate candidate;
        HiraValue *initial = nullptr;
        HiraValue *iteration = nullptr;
        std::size_t binding = 0;
    };
    std::vector<Realized> realized;
    for (const Candidate &candidate : candidates) {
        HiraValue *initial = materializeAtEntry(
            region, *loop, internal, *parent, insertion,
            candidate.address, entryValues);
        if (!initial)
            continue;
        HiraValue *iteration =
            region.createValue(candidate.address->type());
        HiraValue *exit =
            region.createValue(candidate.address->type());
        std::size_t binding =
            loop->addCarriedValue(initial, iteration, exit);
        realized.push_back(
            {candidate, initial, iteration, binding});
    }
    if (realized.empty()) {
        result.detail = "entry-address-not-materializable";
        return result;
    }

    for (const Realized &entry : realized) {
        replaceAddressUses(
            loop->body(), entry.candidate.address,
            entry.iteration);
        loop->body().remove(entry.candidate.definition);
    }

    auto *yield = dynamic_cast<HiraYield *>(
        loop->body().nodes().back().get());
    if (!yield) {
        result.detail = "missing-loop-yield";
        return result;
    }
    std::size_t updatePosition =
        loop->body().nodes().size() - 1;
    for (const Realized &entry : realized) {
        HiraValue *increment =
            region.createIntegerConstant(
                entry.candidate.offsetType,
                entry.candidate.elementStride);
        HiraValue *next =
            region.createValue(entry.iteration->type());
        auto update =
            std::make_unique<HiraComputeOp>(
                ComputeKind::GetElementPtr);
        update->addOperand(entry.iteration);
        update->addOperand(increment);
        update->addResult(next);
        loop->body().insert(
            updatePosition++, std::move(update));
        loop->setCarriedYield(entry.binding, next);
        loop->addYieldValue(next);
        yield->addOperand(next);
    }

    result.changed = true;
    result.recurrences = realized.size();
    region.markModified();
    return result;
}

} // namespace hira::polyhedral
