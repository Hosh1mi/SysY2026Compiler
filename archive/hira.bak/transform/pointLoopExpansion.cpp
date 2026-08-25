#include "../../../include/mid/hira/transform/pointLoopExpansion.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

PointLoopExpansionResult reject(
    PointLoopExpansionError error, std::string detail) {
    PointLoopExpansionResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

const IterationDomain *findDomain(
    const PolyhedralModel &model,
    AffineVariable dimension) {
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

HiraComputeOp *appendCompute(
    HiraSequence &sequence, ComputeKind kind,
    HiraValue *result,
    const std::vector<HiraValue *> &operands,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.append(std::move(owner)));
}

HiraComputeOp *insertCompute(
    HiraSequence &sequence, std::size_t position,
    ComputeKind kind, HiraValue *result,
    const std::vector<HiraValue *> &operands,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.insert(position, std::move(owner)));
}

void mapSourceNode(HiraRegion &region, HiraNode *target,
                   const HiraNode *source) {
    Instruction *instruction =
        region.sourceMapping().sourceInstruction(source);
    if (instruction)
        region.sourceMapping().mapNode(target, instruction);
}

HiraValue *mappedValue(
    const std::map<const HiraValue *, HiraValue *> &mapped,
    const HiraValue *value) {
    auto found = mapped.find(value);
    return found == mapped.end()
               ? const_cast<HiraValue *>(value)
               : found->second;
}

bool sameIntegerValue(
    const HiraValue *left, const HiraValue *right) {
    if (left == right)
        return true;
    return left && right &&
           left->kind() == ValueKind::IntegerConstant &&
           right->kind() == ValueKind::IntegerConstant &&
           left->integerValue() == right->integerValue();
}

bool hasCanonicalInductionUpdate(const HiraLoop &loop) {
    const auto &nodes = loop.body().nodes();
    if (nodes.size() < 2 ||
        loop.yieldValues().size() !=
            loop.carriedValues().size() + 1)
        return false;
    auto *update = dynamic_cast<const HiraComputeOp *>(
        nodes[nodes.size() - 2].get());
    auto *yield = dynamic_cast<const HiraYield *>(
        nodes.back().get());
    if (!update || !yield ||
        update->computeKind() != ComputeKind::Add ||
        update->operands().size() != 2 ||
        update->results().size() != 1 ||
        yield->operands().empty() ||
        yield->operands().front() !=
            update->results().front() ||
        loop.yieldValues().front() !=
            update->results().front())
        return false;
    const HiraValue *left = update->operands()[0];
    const HiraValue *right = update->operands()[1];
    return (left == loop.induction() &&
            sameIntegerValue(right, loop.step())) ||
           (right == loop.induction() &&
            sameIntegerValue(left, loop.step()));
}

bool clonePayloadNode(
    HiraRegion &region, HiraSequence &target,
    const HiraNode &source,
    std::map<const HiraValue *, HiraValue *> &mapped,
    std::uint32_t lane,
    const std::map<const HiraValue *, std::int64_t>
        &addressStrides,
    std::map<const HiraValue *, HiraValue *>
        &laneZeroAddresses) {
    if (auto *compute =
            dynamic_cast<const HiraComputeOp *>(&source)) {
        if (compute->results().size() != 1)
            return false;
        const HiraValue *sourceResult =
            compute->results().front();
        auto stride = addressStrides.find(sourceResult);
        auto base = laneZeroAddresses.find(sourceResult);
        if (!lane && stride != addressStrides.end() &&
            base != laneZeroAddresses.end()) {
            mapped[sourceResult] = base->second;
            return true;
        }
        if (lane && stride != addressStrides.end()) {
            auto *offsetType =
                dynamic_cast<IntegerType *>(
                    compute->operands().back()->type());
            if (base == laneZeroAddresses.end() ||
                !offsetType)
                return false;
            HiraValue *offset = region.createIntegerConstant(
                offsetType,
                stride->second *
                    static_cast<std::int64_t>(lane));
            HiraValue *result =
                region.createValue(sourceResult->type());
            HiraComputeOp *clone = appendCompute(
                target, ComputeKind::GetElementPtr,
                result, {base->second, offset});
            mapSourceNode(region, clone, compute);
            mapped[sourceResult] = result;
            return true;
        }
        std::vector<HiraValue *> operands;
        for (const HiraValue *operand : compute->operands())
            operands.push_back(mappedValue(mapped, operand));
        HiraValue *result = region.createValue(
            compute->results().front()->type());
        HiraComputeOp *clone = appendCompute(
            target, compute->computeKind(), result,
            operands, compute->predicate());
        mapSourceNode(region, clone, compute);
        mapped[sourceResult] = result;
        if (stride != addressStrides.end())
            laneZeroAddresses[sourceResult] = result;
        return true;
    }

    if (auto *load =
            dynamic_cast<const HiraLoad *>(&source)) {
        if (load->results().size() != 1)
            return false;
        auto owner = std::make_unique<HiraLoad>(
            mappedValue(mapped, load->address()));
        HiraValue *result =
            region.createValue(load->results().front()->type());
        owner->addResult(result);
        HiraNode *clone = target.append(std::move(owner));
        mapSourceNode(region, clone, load);
        mapped[load->results().front()] = result;
        return true;
    }

    if (auto *store =
            dynamic_cast<const HiraStore *>(&source)) {
        auto owner = std::make_unique<HiraStore>(
            mappedValue(mapped, store->value()),
            mappedValue(mapped, store->address()));
        HiraNode *clone = target.append(std::move(owner));
        mapSourceNode(region, clone, store);
        return true;
    }

    if (auto *condition =
            dynamic_cast<const HiraIf *>(&source)) {
        auto thenMapped = mapped;
        auto elseMapped = mapped;
        auto owner = std::make_unique<HiraIf>(
            mappedValue(mapped, condition->condition()));
        HiraIf *clone = owner.get();
        for (const auto &node :
             condition->thenSequence().nodes())
            if (!clonePayloadNode(
                    region, clone->thenSequence(), *node,
                    thenMapped, lane, addressStrides,
                    laneZeroAddresses))
                return false;
        for (const auto &node :
             condition->elseSequence().nodes())
            if (!clonePayloadNode(
                    region, clone->elseSequence(), *node,
                    elseMapped, lane, addressStrides,
                    laneZeroAddresses))
                return false;
        for (const HiraIf::ResultBinding &binding :
             condition->resultBindings()) {
            HiraValue *thenValue =
                mappedValue(thenMapped, binding.thenValue);
            HiraValue *elseValue =
                mappedValue(elseMapped, binding.elseValue);
            if (!thenValue || !elseValue ||
                thenValue->type() != elseValue->type())
                return false;
            HiraValue *result =
                region.createValue(binding.result->type());
            clone->addResultBinding(
                thenValue, elseValue, result);
            mapped[binding.result] = result;
        }
        HiraNode *inserted =
            target.append(std::move(owner));
        mapSourceNode(region, inserted, condition);
        return true;
    }
    return false;
}

bool isCloneablePayload(const HiraNode &node) {
    if (dynamic_cast<const HiraComputeOp *>(&node) ||
        dynamic_cast<const HiraLoad *>(&node) ||
        dynamic_cast<const HiraStore *>(&node))
        return true;
    auto *condition = dynamic_cast<const HiraIf *>(&node);
    if (!condition)
        return false;
    for (const auto &armNode :
         condition->thenSequence().nodes())
        if (!isCloneablePayload(*armNode))
            return false;
    for (const auto &armNode :
         condition->elseSequence().nodes())
        if (!isCloneablePayload(*armNode))
            return false;
    return true;
}

std::map<const HiraValue *, std::int64_t>
collectAddressStrides(
    const PolyhedralModel &model,
    AffineVariable dimension,
    const std::set<const HiraNode *> &payload,
    std::vector<const HiraValue *> &addressOrder) {
    std::map<const HiraValue *, std::int64_t> result;
    std::set<const HiraValue *> ambiguous;
    for (const AccessRelation &access : model.accesses()) {
        if (access.statement >= model.statements().size())
            continue;
        const HiraNode *node =
            model.statements()[access.statement].node;
        if (!node || !payload.count(node))
            continue;
        const HiraValue *address = nullptr;
        if (auto *load =
                dynamic_cast<const HiraLoad *>(node))
            address = load->address();
        else if (auto *store =
                     dynamic_cast<const HiraStore *>(node))
            address = store->address();
        if (!address ||
            !payload.count(address->definingNode()))
            continue;
        auto stride = analyzeLinearAccessStride(
            model, access, dimension);
        auto elementSize =
            analyzeAccessElementSize(model, access);
        if (!stride || !elementSize || !*stride ||
            !*elementSize ||
            *stride % *elementSize)
            continue;
        const std::int64_t elementStride =
            static_cast<std::int64_t>(*stride / *elementSize);
        if (ambiguous.count(address))
            continue;
        auto inserted = result.emplace(address, elementStride);
        if (inserted.second)
            addressOrder.push_back(address);
        else if (inserted.first->second != elementStride) {
            result.erase(inserted.first);
            ambiguous.insert(address);
        }
    }
    return result;
}

} // namespace

PointLoopExpansionResult expandPointLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t factor) {
    if (factor < 2 || factor > 8)
        return reject(
            PointLoopExpansionError::UnsupportedFactor,
            "factor-out-of-range");

    const IterationDomain *domain =
        findDomain(model, dimension);
    if (!domain || !domain->loop)
        return reject(
            PointLoopExpansionError::MissingLoopDomain,
            "missing-point-loop");
    auto *pointLoop =
        const_cast<HiraLoop *>(domain->loop);
    auto *indexType = dynamic_cast<IntegerType *>(
        pointLoop->induction()->type());
    if (!hasCanonicalInductionUpdate(*pointLoop) ||
        !indexType ||
        indexType->num_bits_ != 32 ||
        !pointLoop->step() ||
        pointLoop->step()->kind() !=
            ValueKind::IntegerConstant ||
        pointLoop->step()->integerValue() != 1 ||
        pointLoop->role() != HiraLoop::Role::Ordinary)
        return reject(
            PointLoopExpansionError::UnsupportedLoop,
            "non-canonical-point-loop");

    HiraSequence *parent = pointLoop->parent();
    auto position =
        parent ? nodePosition(*parent, pointLoop)
               : std::nullopt;
    if (!parent || !position)
        return reject(
            PointLoopExpansionError::UnsupportedLoop,
            "point-loop-without-parent");

    Loop *sourceLoop =
        region.sourceMapping().sourceLoop(pointLoop);
    Module *module =
        sourceLoop && sourceLoop->header &&
                sourceLoop->header->parent_
            ? sourceLoop->header->parent_->parent_
            : nullptr;
    if (!sourceLoop || !module)
        return reject(
            PointLoopExpansionError::UnsupportedLoop,
            "missing-source-loop");

    const auto &bodyNodes = pointLoop->body().nodes();
    if (bodyNodes.size() < 2)
        return reject(
            PointLoopExpansionError::UnsupportedBody,
            "empty-point-loop");
    const std::size_t payloadCount = bodyNodes.size() - 2;
    if (!payloadCount || payloadCount > 12)
        return reject(
            PointLoopExpansionError::UnsupportedBody,
            "point-body-cost");
    for (std::size_t index = 0;
         index < payloadCount; ++index)
        if (!isCloneablePayload(*bodyNodes[index]))
            return reject(
                PointLoopExpansionError::UnsupportedBody,
                "non-straight-line-point-body");
    std::set<const HiraNode *> payload;
    for (std::size_t index = 0;
         index < payloadCount; ++index)
        payload.insert(bodyNodes[index].get());
    // Keep first structural access order separate from the pointer-keyed
    // lookup table; pointer ordering is process-dependent.
    std::vector<const HiraValue *> addressOrder;
    const std::map<const HiraValue *, std::int64_t>
        addressStrides = collectAddressStrides(
            model, dimension, payload, addressOrder);

    HiraValue *oldLower = pointLoop->lowerBound();
    HiraValue *oldUpper = pointLoop->upperBound();
    const std::int64_t laneOffset =
        static_cast<std::int64_t>(factor - 1);
    HiraValue *minimumSafeUpper =
        region.createIntegerConstant(
            indexType,
            static_cast<std::int64_t>(
                std::numeric_limits<std::int32_t>::min()) +
                laneOffset);
    HiraValue *safeUpper =
        region.createValue(module->int1_ty_);
    insertCompute(
        *parent, (*position)++, ComputeKind::ICmp,
        safeUpper, {oldUpper, minimumSafeUpper},
        ICmpInst::ICMP_SGE);
    HiraValue *offset =
        region.createIntegerConstant(indexType, laneOffset);
    HiraValue *adjustedUpper =
        region.createValue(indexType);
    insertCompute(
        *parent, (*position)++, ComputeKind::Sub,
        adjustedUpper, {oldUpper, offset});
    HiraValue *mainUpper = region.createValue(indexType);
    insertCompute(
        *parent, (*position)++, ComputeKind::Select,
        mainUpper, {safeUpper, adjustedUpper, oldLower});

    HiraValue *mainStep =
        region.createIntegerConstant(indexType, factor);
    HiraValue *mainInduction =
        region.createValue(indexType);
    auto mainOwner = std::make_unique<HiraLoop>(
        mainInduction, oldLower, mainUpper, mainStep);
    HiraLoop *mainLoop = mainOwner.get();
    region.sourceMapping().mapLoop(mainLoop, sourceLoop);

    struct AddressRecurrence {
        const HiraValue *sourceAddress = nullptr;
        HiraValue *iteration = nullptr;
        std::size_t binding = 0;
        std::int64_t stride = 0;
        IntegerType *offsetType = nullptr;
    };
    std::vector<AddressRecurrence> addressRecurrences;
    for (const HiraValue *sourceAddress : addressOrder) {
        auto stride = addressStrides.find(sourceAddress);
        if (stride == addressStrides.end())
            continue;
        auto *definition =
            dynamic_cast<HiraComputeOp *>(
                sourceAddress->definingNode());
        if (!definition ||
            definition->computeKind() !=
                ComputeKind::GetElementPtr ||
            definition->results().size() != 1 ||
            definition->operands().size() < 2)
            continue;
        bool availableAtEntry = true;
        std::vector<HiraValue *> initialOperands;
        for (HiraValue *operand : definition->operands()) {
            if (operand == pointLoop->induction()) {
                initialOperands.push_back(oldLower);
                continue;
            }
            HiraNode *operandDefinition =
                operand->definingNode();
            if (operandDefinition &&
                payload.count(operandDefinition)) {
                availableAtEntry = false;
                break;
            }
            initialOperands.push_back(operand);
        }
        auto *offsetType =
            dynamic_cast<IntegerType *>(
                definition->operands().back()->type());
        if (!availableAtEntry || !offsetType)
            continue;

        HiraValue *initial =
            region.createValue(sourceAddress->type());
        HiraComputeOp *initialNode = insertCompute(
            *parent, (*position)++,
            ComputeKind::GetElementPtr, initial,
            initialOperands);
        mapSourceNode(region, initialNode, definition);
        HiraValue *iteration =
            region.createValue(sourceAddress->type());
        HiraValue *result =
            region.createValue(sourceAddress->type());
        std::size_t binding =
            mainLoop->addCarriedValue(
                initial, iteration, result);
        addressRecurrences.push_back(
            {sourceAddress, iteration, binding,
             stride->second, offsetType});
    }

    HiraValue *cursorIteration =
        region.createValue(indexType);
    HiraValue *mainEnd =
        region.createValue(indexType);
    const std::size_t cursorBinding =
        mainLoop->addCarriedValue(
            oldLower, cursorIteration, mainEnd);

    std::vector<HiraValue *> carriedIterations;
    std::vector<HiraValue *> carriedResults;
    std::vector<std::size_t> carriedBindings;
    for (const HiraLoop::CarriedBinding &binding :
         pointLoop->carriedValues()) {
        HiraValue *iteration =
            region.createValue(binding.iteration->type());
        HiraValue *result =
            region.createValue(binding.result->type());
        carriedBindings.push_back(
            mainLoop->addCarriedValue(
                binding.initial, iteration, result));
        carriedIterations.push_back(iteration);
        carriedResults.push_back(result);
    }

    HiraSequence &mainBody = mainLoop->body();
    std::vector<HiraValue *> carriedState =
        carriedIterations;
    std::map<const HiraValue *, HiraValue *>
        laneZeroAddresses;
    for (const AddressRecurrence &recurrence :
         addressRecurrences)
        laneZeroAddresses[recurrence.sourceAddress] =
            recurrence.iteration;
    for (std::uint32_t lane = 0;
         lane < factor; ++lane) {
        std::map<const HiraValue *, HiraValue *> mapped;
        HiraValue *laneInduction = mainInduction;
        if (lane) {
            HiraValue *laneOffsetValue =
                region.createIntegerConstant(indexType, lane);
            laneInduction = region.createValue(indexType);
            appendCompute(
                mainBody, ComputeKind::Add,
                laneInduction,
                {mainInduction, laneOffsetValue});
        }
        mapped[pointLoop->induction()] = laneInduction;
        for (std::size_t index = 0;
             index < carriedState.size(); ++index)
            mapped[pointLoop->carriedValues()[index].iteration] =
                carriedState[index];

        for (std::size_t index = 0;
             index < payloadCount; ++index)
            if (!clonePayloadNode(
                    region, mainBody,
                    *bodyNodes[index], mapped, lane,
                    addressStrides,
                    laneZeroAddresses))
                return reject(
                    PointLoopExpansionError::UnsupportedBody,
                    "failed-to-clone-point-body");

        for (std::size_t index = 0;
             index < carriedState.size(); ++index)
            carriedState[index] = mappedValue(
                mapped,
                pointLoop->carriedValues()[index].yielded);
    }

    HiraValue *mainNext = region.createValue(indexType);
    appendCompute(
        mainBody, ComputeKind::Add,
        mainNext, {mainInduction, mainStep});
    mainLoop->addYieldValue(mainNext);
    std::vector<HiraValue *> bindingYields(
        mainLoop->carriedValues().size(), nullptr);
    mainLoop->setCarriedYield(cursorBinding, mainNext);
    bindingYields[cursorBinding] = mainNext;
    for (std::size_t index = 0;
         index < carriedState.size(); ++index) {
        mainLoop->setCarriedYield(carriedBindings[index],
                                  carriedState[index]);
        bindingYields[carriedBindings[index]] =
            carriedState[index];
    }
    for (const AddressRecurrence &recurrence :
         addressRecurrences) {
        HiraValue *increment =
            region.createIntegerConstant(
                recurrence.offsetType,
                recurrence.stride *
                    static_cast<std::int64_t>(factor));
        HiraValue *next =
            region.createValue(
                recurrence.iteration->type());
        appendCompute(
            mainBody, ComputeKind::GetElementPtr,
            next, {recurrence.iteration, increment});
        mainLoop->setCarriedYield(
            recurrence.binding, next);
        bindingYields[recurrence.binding] = next;
    }
    for (HiraValue *yielded : bindingYields)
        mainLoop->addYieldValue(yielded);
    auto mainYield = std::make_unique<HiraYield>();
    mainYield->addOperand(mainNext);
    for (HiraValue *yielded : bindingYields)
        mainYield->addOperand(yielded);
    mainBody.append(std::move(mainYield));

    parent->insert(*position, std::move(mainOwner));
    pointLoop->setLowerBound(mainEnd);
    for (std::size_t index = 0;
         index < carriedResults.size(); ++index)
        pointLoop->setCarriedInitial(
            index, carriedResults[index]);
    pointLoop->setRole(HiraLoop::Role::ScalarRemainder);
    region.markModified();
    return {true, PointLoopExpansionError::None, {}};
}

const char *pointLoopExpansionErrorName(
    PointLoopExpansionError error) {
    switch (error) {
    case PointLoopExpansionError::None:
        return "none";
    case PointLoopExpansionError::MissingLoopDomain:
        return "missing-loop-domain";
    case PointLoopExpansionError::UnsupportedLoop:
        return "unsupported-loop";
    case PointLoopExpansionError::UnsupportedBody:
        return "unsupported-body";
    case PointLoopExpansionError::UnsupportedFactor:
        return "unsupported-factor";
    }
    return "unknown";
}

} // namespace hira::polyhedral
