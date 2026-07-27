#include "../../../include/mid/hira/transform/loopVectorization.hpp"
#include "../../../include/mid/hira/transform/loopAddressRecurrence.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
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

LoopVectorizationResult reject(
    LoopVectorizationError error, std::string detail) {
    LoopVectorizationResult result;
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

const AccessRelation *findAccess(
    const PolyhedralModel &model, const HiraNode *node) {
    const PolyhedralStatement *statement = nullptr;
    for (const PolyhedralStatement &candidate :
         model.statements()) {
        if (candidate.node != node)
            continue;
        if (statement)
            return nullptr;
        statement = &candidate;
    }
    if (!statement)
        return nullptr;

    const AccessRelation *result = nullptr;
    for (const AccessRelation &access : model.accesses()) {
        if (access.statement != statement->id)
            continue;
        if (result)
            return nullptr;
        result = &access;
    }
    return result;
}

bool isVectorScalarType(Type *type) {
    if (!type)
        return false;
    if (type->tid_ == Type::FloatTyID)
        return true;
    auto *integer = dynamic_cast<IntegerType *>(type);
    return integer && integer->num_bits_ == 32;
}

bool isVectorCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::ICmp:
    case ComputeKind::Select:
        return true;
    default:
        return false;
    }
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

bool canMaterializeAddress(ComputeKind kind) {
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

HiraValue *materializeAddressAtEntry(
    HiraRegion &region, const HiraLoop &loop,
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
        !canMaterializeAddress(compute->computeKind()))
        return nullptr;

    auto owner = std::make_unique<HiraComputeOp>(
        compute->computeKind(), compute->predicate());
    for (const HiraValue *operand : compute->operands()) {
        HiraValue *materialized = materializeAddressAtEntry(
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

HiraComputeOp *appendPointerOffset(
    HiraSequence &sequence, HiraValue *pointer,
    HiraValue *offset, HiraValue *result) {
    return appendCompute(
        sequence, ComputeKind::GetElementPtr, result,
        {pointer, offset});
}


struct AccessMode {
    bool varying = false;
    Type *elementType = nullptr;
};

struct PointerRecurrence {
    HiraValue *scalarAddress = nullptr;
    HiraValue *iteration = nullptr;
    std::size_t binding = 0;
};

std::optional<AccessMode> analyzeAccess(
    const PolyhedralModel &model, const HiraNode &node,
    AffineVariable dimension) {
    const AccessRelation *access =
        findAccess(model, &node);
    if (!access)
        return std::nullopt;
    auto stride = analyzeLinearAccessStride(
        model, *access, dimension);
    auto elementSize =
        analyzeAccessElementSize(model, *access);
    if (!stride || !elementSize ||
        (*stride != 0 && *stride != *elementSize))
        return std::nullopt;

    Type *elementType = nullptr;
    if (auto *load =
            dynamic_cast<const HiraLoad *>(&node))
        elementType =
            load->results().empty()
                ? nullptr
                : load->results().front()->type();
    else if (auto *store =
                 dynamic_cast<const HiraStore *>(&node))
        elementType = store->value()->type();
    if (!elementType)
        return std::nullopt;
    return AccessMode{*stride != 0, elementType};
}

bool collectAddressDefinitions(
    const HiraValue *value,
    const std::set<const HiraNode *> &payload,
    std::set<const HiraNode *> &addressNodes) {
    const HiraNode *definition =
        value ? value->definingNode() : nullptr;
    if (!definition || !payload.count(definition))
        return true;
    auto *compute =
        dynamic_cast<const HiraComputeOp *>(definition);
    if (!compute)
        return false;
    if (!addressNodes.insert(compute).second)
        return true;
    for (const HiraValue *operand : compute->operands())
        if (!collectAddressDefinitions(
                operand, payload, addressNodes))
            return false;
    return true;
}

void mapSourceNode(
    HiraRegion &region, HiraNode *target,
    const HiraNode *source) {
    Instruction *instruction =
        region.sourceMapping().sourceInstruction(source);
    if (instruction)
        region.sourceMapping().mapNode(
            target, instruction);
}

std::size_t countValueUses(
    const HiraSequence &sequence, const HiraValue *value) {
    std::size_t uses = 0;
    for (const auto &owner : sequence.nodes()) {
        for (const HiraValue *operand : owner->operands())
            uses += operand == value;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(owner.get()))
            uses += countValueUses(loop->body(), value);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(owner.get())) {
            uses += countValueUses(
                condition->thenSequence(), value);
            uses += countValueUses(
                condition->elseSequence(), value);
        }
    }
    return uses;
}

struct AddReductionBinding {
    const HiraLoop::CarriedBinding *binding = nullptr;
};

std::optional<AddReductionBinding> recognizeAddReduction(
    const HiraLoop &loop) {
    if (loop.carriedValues().size() != 1)
        return std::nullopt;
    const HiraLoop::CarriedBinding &binding =
        loop.carriedValues().front();
    if (!binding.initial || !binding.iteration ||
        !binding.yielded || !binding.result)
        return std::nullopt;
    auto *update = dynamic_cast<HiraComputeOp *>(
        binding.yielded->definingNode());
    if (!update || update->computeKind() != ComputeKind::Add ||
        update->operands().size() != 2)
        return std::nullopt;
    const bool usesIteration =
        update->operands()[0] == binding.iteration ||
        update->operands()[1] == binding.iteration;
    if (!usesIteration ||
        (update->operands()[0] == binding.iteration &&
         update->operands()[1] == binding.iteration))
        return std::nullopt;
    return AddReductionBinding{&binding};
}

HiraValue *horizontalReduce(
    HiraRegion &region, HiraSequence &parent,
    std::size_t &position, Module *module,
    HiraValue *vectorValue, Type *scalarType,
    std::uint32_t lanes) {
    HiraValue *sum = nullptr;
    for (std::uint32_t lane = 0; lane < lanes; ++lane) {
        HiraValue *index = region.createIntegerConstant(
            module->int32_ty_, static_cast<std::int64_t>(lane));
        HiraValue *laneValue =
            region.createValue(scalarType);
        insertCompute(
            parent, position++, ComputeKind::ExtractElement,
            laneValue, {vectorValue, index});
        if (!sum)
            sum = laneValue;
        else {
            HiraValue *next = region.createValue(scalarType);
            insertCompute(
                parent, position++, ComputeKind::Add, next,
                {sum, laneValue});
            sum = next;
        }
    }
    return sum;
}

} // namespace

LoopVectorizationResult vectorizeLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t lanes) {
    if (lanes != 4)
        return reject(
            LoopVectorizationError::UnsupportedType,
            "unsupported-vector-width");

    const IterationDomain *domain =
        findDomain(model, dimension);
    if (!domain || !domain->loop)
        return reject(
            LoopVectorizationError::MissingLoopDomain,
            "missing-vector-loop");
    auto *pointLoop =
        const_cast<HiraLoop *>(domain->loop);
    auto control =
        analyzeCanonicalLoopControl(*pointLoop);
    const HiraComputeOp *inductionUpdate =
        control ? control->inductionUpdate
                : findInductionUpdate(*pointLoop);
    auto *indexType =
        dynamic_cast<IntegerType *>(
            pointLoop->induction()->type());
    if (!inductionUpdate || !indexType ||
        indexType->num_bits_ != 32 ||
        !pointLoop->step() ||
        pointLoop->step()->kind() !=
            ValueKind::IntegerConstant ||
        pointLoop->step()->integerValue() != 1)
        return reject(
            LoopVectorizationError::UnsupportedLoop,
            "non-canonical-vector-loop");

    std::optional<AddReductionBinding> addReduction =
        recognizeAddReduction(*pointLoop);
    if (!addReduction &&
        !pointLoop->carriedValues().empty())
        return reject(
            LoopVectorizationError::UnsupportedLoop,
            "non-canonical-vector-loop");

    HiraSequence *parent = pointLoop->parent();
    auto position =
        parent ? nodePosition(*parent, pointLoop)
               : std::nullopt;
    if (!parent || !position)
        return reject(
            LoopVectorizationError::UnsupportedLoop,
            "vector-loop-without-parent");

    Loop *sourceLoop =
        region.sourceMapping().sourceLoop(pointLoop);
    Module *module =
        sourceLoop && sourceLoop->header &&
                sourceLoop->header->parent_
            ? sourceLoop->header->parent_->parent_
            : nullptr;
    if (!sourceLoop || !module)
        return reject(
            LoopVectorizationError::UnsupportedLoop,
            "missing-vector-source-loop");

    const std::size_t payloadCount =
        pointLoop->body().nodes().size() - 2;
    std::set<const HiraNode *> payload;
    for (std::size_t index = 0;
         index < payloadCount; ++index) {
        const HiraNode *node =
            pointLoop->body().nodes()[index].get();
        if (!dynamic_cast<const HiraComputeOp *>(node) &&
            !dynamic_cast<const HiraLoad *>(node) &&
            !dynamic_cast<const HiraStore *>(node))
            return reject(
                LoopVectorizationError::UnsupportedBody,
                "non-straight-line-vector-body");
        payload.insert(node);
    }

    std::map<const HiraNode *, AccessMode> accessModes;
    std::set<const HiraNode *> addressNodes;
    for (const HiraNode *node : payload) {
        if (auto *load =
                dynamic_cast<const HiraLoad *>(node)) {
            auto mode =
                analyzeAccess(model, *load, dimension);
            if (!mode ||
                (mode->varying &&
                 !isVectorScalarType(
                     mode->elementType)))
                return reject(
                    LoopVectorizationError::
                        UnsupportedAccess,
                    "unsupported-vector-load");
            accessModes.emplace(node, *mode);
            if (!collectAddressDefinitions(
                    load->address(), payload,
                    addressNodes))
                return reject(
                    LoopVectorizationError::
                        UnsupportedAccess,
                    "unsupported-load-address");
        } else if (auto *store =
                       dynamic_cast<const HiraStore *>(node)) {
            auto mode =
                analyzeAccess(model, *store, dimension);
            if (!mode || !mode->varying ||
                !isVectorScalarType(
                    mode->elementType))
                return reject(
                    LoopVectorizationError::
                        UnsupportedAccess,
                    "unsupported-vector-store");
            accessModes.emplace(node, *mode);
            if (!collectAddressDefinitions(
                    store->address(), payload,
                    addressNodes))
                return reject(
                    LoopVectorizationError::
                        UnsupportedAccess,
                    "unsupported-store-address");
        }
    }

    std::set<const HiraValue *> varyingData;
    // Induction used as ordinary data produces consecutive lane values
    // [i, i+1, ..., i+lanes-1] in the vector main loop.
    bool inductionUsedAsData = false;
    for (const HiraNode *node : payload) {
        for (std::size_t operandIndex = 0;
             operandIndex < node->operands().size();
             ++operandIndex) {
            const HiraValue *operand =
                node->operands()[operandIndex];
            bool addressUse =
                (dynamic_cast<const HiraLoad *>(node) &&
                 operandIndex == 0) ||
                (dynamic_cast<const HiraStore *>(node) &&
                 operandIndex == 1) ||
                addressNodes.count(node);
            if (operand == pointLoop->induction() && !addressUse)
                inductionUsedAsData = true;
        }
    }
    if (inductionUsedAsData)
        varyingData.insert(pointLoop->induction());

    for (const HiraNode *node : payload) {
        if (auto *load =
                dynamic_cast<const HiraLoad *>(node)) {
            if (accessModes.at(node).varying)
                varyingData.insert(
                    load->results().front());
            continue;
        }
        auto *compute =
            dynamic_cast<const HiraComputeOp *>(node);
        if (!compute || addressNodes.count(node))
            continue;
        bool varying = false;
        for (const HiraValue *operand : compute->operands())
            varying |= varyingData.count(operand);
        if (compute->computeKind() == ComputeKind::Select &&
            varying &&
            !varyingData.count(compute->operands().front()))
            return reject(
                LoopVectorizationError::UnsupportedBody,
                "non-vector-select-condition");
        if (varying)
            varyingData.insert(compute->results().front());
    }

    for (const HiraNode *node : payload) {
        auto *compute =
            dynamic_cast<const HiraComputeOp *>(node);
        if (compute && !addressNodes.count(node) &&
            !isVectorCompute(compute->computeKind()))
            return reject(
                LoopVectorizationError::UnsupportedBody,
                "unsupported-vector-compute");

        for (std::size_t operandIndex = 0;
             operandIndex < node->operands().size();
             ++operandIndex) {
            const HiraValue *operand =
                node->operands()[operandIndex];
            const HiraNode *definition =
                operand ? operand->definingNode() : nullptr;
            bool addressUse =
                (dynamic_cast<const HiraLoad *>(node) &&
                 operandIndex == 0) ||
                (dynamic_cast<const HiraStore *>(node) &&
                 operandIndex == 1) ||
                addressNodes.count(node);
            if (definition &&
                addressNodes.count(definition) &&
                !addressUse)
                return reject(
                    LoopVectorizationError::
                        UnsupportedBody,
                    "address-value-used-as-data");
        }
    }

    if (addReduction) {
        if (countValueUses(
                pointLoop->body(),
                addReduction->binding->iteration) != 1)
            return reject(
                LoopVectorizationError::UnsupportedBody,
                "non-vector-reduction-use");
        if (!isVectorScalarType(
                addReduction->binding->iteration->type()))
            return reject(
                LoopVectorizationError::UnsupportedType,
                "unsupported-reduction-type");
    }

    HiraValue *oldLower = pointLoop->lowerBound();
    HiraValue *oldUpper = pointLoop->upperBound();
    // Two contiguous vector parts halve loop-control overhead on A53.
    const std::uint32_t unrollFactor =
        addReduction ? 1U : 2U;
    const std::uint32_t vectorTrip = lanes * unrollFactor;
    const std::int64_t laneOffset =
        static_cast<std::int64_t>(vectorTrip - 1);
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
        region.createIntegerConstant(
            indexType, laneOffset);
    HiraValue *adjustedUpper =
        region.createValue(indexType);
    insertCompute(
        *parent, (*position)++, ComputeKind::Sub,
        adjustedUpper, {oldUpper, offset});
    HiraValue *vectorUpper =
        region.createValue(indexType);
    insertCompute(
        *parent, (*position)++, ComputeKind::Select,
        vectorUpper,
        {safeUpper, adjustedUpper, oldLower});

    HiraValue *vectorStep =
        region.createIntegerConstant(indexType, vectorTrip);
    HiraValue *vectorInduction =
        region.createValue(indexType);
    auto vectorOwner = std::make_unique<HiraLoop>(
        vectorInduction, oldLower, vectorUpper,
        vectorStep);
    HiraLoop *vectorLoop = vectorOwner.get();
    vectorLoop->setRole(HiraLoop::Role::VectorMain);
    region.sourceMapping().mapLoop(
        vectorLoop, sourceLoop);

    HiraValue *cursorIteration =
        region.createValue(indexType);
    HiraValue *vectorEnd =
        region.createValue(indexType);
    const std::size_t cursorBinding =
        vectorLoop->addCarriedValue(
            oldLower, cursorIteration, vectorEnd);

    std::map<const HiraValue *, HiraValue *> mapped;
    std::set<const HiraValue *> vectorValues;
    std::map<HiraValue *, HiraValue *> splats;
    mapped[pointLoop->induction()] = vectorInduction;
    HiraSequence &vectorBody = vectorLoop->body();

    auto vectorType = [&](Type *scalar) -> Type * {
        return module->get_vector_type(
            scalar, lanes);
    };

    std::optional<std::size_t> accBinding;
    HiraValue *vectorAccExit = nullptr;
    if (addReduction) {
        Type *scalarAccType =
            addReduction->binding->iteration->type();
        HiraValue *vectorAccIteration =
            region.createValue(vectorType(scalarAccType));
        vectorAccExit =
            region.createValue(vectorType(scalarAccType));
        HiraValue *vectorAccInitial =
            region.createValue(vectorType(scalarAccType));
        HiraValue *zeroScalar =
            region.createIntegerConstant(scalarAccType, 0);
        insertCompute(
            *parent, (*position)++, ComputeKind::Splat,
            vectorAccInitial, {zeroScalar});
        accBinding = vectorLoop->addCarriedValue(
            vectorAccInitial, vectorAccIteration,
            vectorAccExit);
        mapped[addReduction->binding->iteration] =
            vectorAccIteration;
        vectorValues.insert(
            addReduction->binding->iteration);
    }

    auto mappedValue =
        [&](const HiraValue *value) -> HiraValue * {
        auto found = mapped.find(value);
        return found == mapped.end()
                   ? const_cast<HiraValue *>(value)
                   : found->second;
    };
    auto splat = [&](HiraValue *scalar) -> HiraValue * {
        auto found = splats.find(scalar);
        if (found != splats.end())
            return found->second;
        const HiraNode *definition =
            scalar ? scalar->definingNode() : nullptr;
        const bool hoistInvariant =
            definition && !payload.count(definition);
        HiraValue *packed =
            region.createValue(
                vectorType(scalar->type()));
        if (hoistInvariant)
            insertCompute(
                *parent, (*position)++,
                ComputeKind::Splat, packed, {scalar});
        else
            appendCompute(
                vectorBody, ComputeKind::Splat,
                packed, {scalar});
        splats.emplace(scalar, packed);
        return packed;
    };
    HiraValue *laneInduction = nullptr;
    auto inductionDataValue = [&]() -> HiraValue * {
        if (laneInduction)
            return laneInduction;
        HiraValue *base = splat(vectorInduction);
        HiraValue *zero =
            region.createIntegerConstant(indexType, 0);
        HiraValue *offsets =
            region.createValue(vectorType(indexType));
        appendCompute(
            vectorBody, ComputeKind::Splat, offsets, {zero});
        for (std::uint32_t lane = 1; lane < lanes; ++lane) {
            HiraValue *laneValue =
                region.createIntegerConstant(
                    indexType, static_cast<std::int64_t>(lane));
            HiraValue *laneIndex =
                region.createIntegerConstant(
                    module->int32_ty_,
                    static_cast<std::int64_t>(lane));
            HiraValue *next =
                region.createValue(vectorType(indexType));
            appendCompute(
                vectorBody, ComputeKind::InsertElement, next,
                {offsets, laneValue, laneIndex});
            offsets = next;
        }
        laneInduction =
            region.createValue(vectorType(indexType));
        appendCompute(
            vectorBody, ComputeKind::Add, laneInduction,
            {base, offsets});
        return laneInduction;
    };
    auto mapOperand =
        [&](const HiraValue *operand,
            bool asData) -> HiraValue * {
        if (asData && operand == pointLoop->induction() &&
            inductionUsedAsData)
            return inductionDataValue();
        return mappedValue(operand);
    };

    std::set<const HiraNode *> addressInternal = payload;
    addressInternal.insert(addressNodes.begin(),
                           addressNodes.end());
    std::vector<PointerRecurrence> pointerRecurrences;
    std::map<HiraValue *, HiraValue *> pointerAtScalarAddress;
    std::set<HiraValue *> varyingScalarAddresses;
    for (std::size_t index = 0;
         index < payloadCount; ++index) {
        const HiraNode *node =
            pointLoop->body().nodes()[index].get();
        const AccessMode *mode = nullptr;
        if (auto *load =
                dynamic_cast<const HiraLoad *>(node))
            mode = &accessModes.at(node);
        else if (auto *store =
                     dynamic_cast<const HiraStore *>(node))
            mode = &accessModes.at(node);
        if (!mode || !mode->varying)
            continue;
        HiraValue *scalarAddress = nullptr;
        if (auto *load =
                dynamic_cast<const HiraLoad *>(node))
            scalarAddress = load->address();
        else
            scalarAddress =
                dynamic_cast<const HiraStore *>(node)
                    ->address();
        if (!scalarAddress ||
            !varyingScalarAddresses.insert(scalarAddress)
                 .second)
            continue;
        std::map<const HiraValue *, HiraValue *> entryCache;
        std::size_t insertPosition = *position;
        HiraValue *initial = materializeAddressAtEntry(
            region, *pointLoop, addressInternal, *parent,
            insertPosition, scalarAddress, entryCache);
        *position = insertPosition;
        if (!initial)
            return reject(
                LoopVectorizationError::UnsupportedAccess,
                "unsupported-vector-address");
        HiraValue *iteration =
            region.createValue(scalarAddress->type());
        HiraValue *exit =
            region.createValue(scalarAddress->type());
        std::size_t binding =
            vectorLoop->addCarriedValue(
                initial, iteration, exit);
        pointerRecurrences.push_back(
            {scalarAddress, iteration, binding});
        pointerAtScalarAddress[scalarAddress] = iteration;
    }

    auto pointerForPart =
        [&](HiraValue *scalarAddress,
            std::uint32_t part) -> HiraValue * {
        auto found = pointerAtScalarAddress.find(
            scalarAddress);
        if (found == pointerAtScalarAddress.end())
            return nullptr;
        if (part == 0)
            return found->second;
        HiraValue *offset =
            region.createIntegerConstant(
                indexType,
                static_cast<std::int64_t>(lanes * part));
        HiraValue *bumped =
            region.createValue(found->second->type());
        appendPointerOffset(
            vectorBody, found->second, offset, bumped);
        return bumped;
    };

    bool vectorPartOk = true;
    for (std::uint32_t part = 0; part < unrollFactor;
         ++part) {
        std::map<const HiraValue *, HiraValue *> partMapped =
            mapped;
        std::set<const HiraValue *> partVectorValues;
        auto partMappedValue =
            [&](const HiraValue *value) -> HiraValue * {
            auto found = partMapped.find(value);
            return found == partMapped.end()
                       ? const_cast<HiraValue *>(value)
                       : found->second;
        };
        auto partMapOperand =
            [&](const HiraValue *operand,
                bool asData) -> HiraValue * {
            if (asData && operand == pointLoop->induction() &&
                inductionUsedAsData)
                return inductionDataValue();
            return partMappedValue(operand);
        };
        for (std::size_t index = 0;
             index < payloadCount; ++index) {
            if (!vectorPartOk)
                break;
            const HiraNode *node =
                pointLoop->body().nodes()[index].get();
            if (auto *compute =
                    dynamic_cast<const HiraComputeOp *>(node)) {
                if (addressNodes.count(node)) {
                    const HiraValue *result =
                        compute->results().front();
                    if (pointerAtScalarAddress.count(
                            const_cast<HiraValue *>(result)))
                        continue;
                }
                const bool addressCompute =
                    addressNodes.count(node);
                bool vectorResult = !addressCompute;
                if (vectorResult) {
                    vectorResult = false;
                    for (const HiraValue *operand :
                         compute->operands())
                        vectorResult |=
                            partVectorValues.count(operand) ||
                            (inductionUsedAsData &&
                             operand ==
                                 pointLoop->induction());
                }

                std::vector<HiraValue *> operands;
                for (const HiraValue *operand :
                     compute->operands()) {
                    const bool operandIsVector =
                        !addressCompute &&
                        (partVectorValues.count(operand) ||
                         (inductionUsedAsData &&
                          operand ==
                              pointLoop->induction()));
                    HiraValue *mappedOperand =
                        partMapOperand(operand, !addressCompute);
                    if (vectorResult && !operandIsVector)
                        mappedOperand = splat(mappedOperand);
                    operands.push_back(mappedOperand);
                }
                Type *resultType =
                    vectorResult
                        ? (compute->computeKind() ==
                                   ComputeKind::ICmp
                               ? vectorType(
                                     module->int32_ty_)
                               : vectorType(
                                     compute->results()
                                         .front()
                                         ->type()))
                        : compute->results()
                              .front()
                              ->type();
                HiraValue *result =
                    region.createValue(resultType);
                HiraComputeOp *clone = appendCompute(
                    vectorBody, compute->computeKind(),
                    result, operands, compute->predicate());
                mapSourceNode(region, clone, compute);
                partMapped[compute->results().front()] =
                    result;
                if (vectorResult)
                    partVectorValues.insert(
                        compute->results().front());
                continue;
            }

            if (auto *load =
                    dynamic_cast<const HiraLoad *>(node)) {
                const AccessMode &mode =
                    accessModes.at(node);
                HiraValue *address =
                    pointerForPart(load->address(), part);
                if (!address)
                    address = partMapOperand(
                        load->address(), false);
                Type *resultType =
                    load->results().front()->type();
                if (mode.varying) {
                    resultType = vectorType(resultType);
                    HiraValue *vectorAddress =
                        region.createValue(
                            module->get_pointer_type(
                                resultType));
                    appendCompute(
                        vectorBody, ComputeKind::BitCast,
                        vectorAddress, {address});
                    address = vectorAddress;
                }
                HiraValue *result =
                    region.createValue(resultType);
                auto owner =
                    std::make_unique<HiraLoad>(address);
                owner->addResult(result);
                HiraNode *clone =
                    vectorBody.append(std::move(owner));
                mapSourceNode(region, clone, load);
                partMapped[load->results().front()] =
                    result;
                if (mode.varying)
                    partVectorValues.insert(
                        load->results().front());
                continue;
            }

            auto *store =
                dynamic_cast<const HiraStore *>(node);
            if (!store) {
                vectorPartOk = false;
                break;
            }
            const bool storedIsVector =
                partVectorValues.count(store->value()) ||
                (inductionUsedAsData &&
                 store->value() == pointLoop->induction());
            HiraValue *stored =
                partMapOperand(store->value(), true);
            if (!storedIsVector)
                stored = splat(stored);
            HiraValue *address =
                pointerForPart(store->address(), part);
            if (!address)
                address = partMapOperand(store->address(), false);
            HiraValue *vectorAddress =
                region.createValue(
                    module->get_pointer_type(
                        stored->type()));
            appendCompute(
                vectorBody, ComputeKind::BitCast,
                vectorAddress, {address});
            auto owner =
                std::make_unique<HiraStore>(
                    stored, vectorAddress);
            HiraNode *clone =
                vectorBody.append(std::move(owner));
            mapSourceNode(region, clone, store);
        }
    }
    if (!vectorPartOk)
        return reject(
            LoopVectorizationError::UnsupportedBody,
            "unexpected-vector-node");

    HiraValue *vectorNext =
        region.createValue(indexType);
    appendCompute(
        vectorBody, ComputeKind::Add,
        vectorNext, {vectorInduction, vectorStep});
    std::vector<HiraValue *> bindingYields(
        vectorLoop->carriedValues().size(), nullptr);
    vectorLoop->setCarriedYield(
        cursorBinding, vectorNext);
    bindingYields[cursorBinding] = vectorNext;
    if (addReduction && accBinding) {
        HiraValue *vectorAccNext = mappedValue(
            addReduction->binding->yielded);
        vectorLoop->setCarriedYield(
            *accBinding, vectorAccNext);
        bindingYields[*accBinding] = vectorAccNext;
    }
    for (const PointerRecurrence &recurrence :
         pointerRecurrences) {
        HiraValue *nextPointer =
            region.createValue(recurrence.iteration->type());
        appendPointerOffset(
            vectorBody, recurrence.iteration, vectorStep,
            nextPointer);
        vectorLoop->setCarriedYield(
            recurrence.binding, nextPointer);
        bindingYields[recurrence.binding] = nextPointer;
    }
    vectorLoop->addYieldValue(vectorNext);
    for (HiraValue *yielded : bindingYields)
        vectorLoop->addYieldValue(yielded);
    auto yield = std::make_unique<HiraYield>();
    yield->addOperand(vectorNext);
    for (HiraValue *yielded : bindingYields)
        yield->addOperand(yielded);
    vectorBody.append(std::move(yield));

    parent->insert(*position, std::move(vectorOwner));
    if (addReduction && vectorAccExit) {
        std::size_t reducePosition = *position + 1;
        HiraValue *partialSum = horizontalReduce(
            region, *parent, reducePosition, module,
            vectorAccExit,
            addReduction->binding->iteration->type(),
            lanes);
        HiraValue *scalarAcc =
            region.createValue(
                addReduction->binding->iteration->type());
        insertCompute(
            *parent, reducePosition++, ComputeKind::Add,
            scalarAcc,
            {addReduction->binding->initial, partialSum});
        pointLoop->setCarriedInitial(0, scalarAcc);
    }
    pointLoop->setLowerBound(vectorEnd);
    pointLoop->setRole(
        HiraLoop::Role::ScalarRemainder);
    region.markModified();
    return {true, LoopVectorizationError::None, {}};
}

const char *loopVectorizationErrorName(
    LoopVectorizationError error) {
    switch (error) {
    case LoopVectorizationError::None:
        return "none";
    case LoopVectorizationError::MissingLoopDomain:
        return "missing-loop-domain";
    case LoopVectorizationError::UnsupportedLoop:
        return "unsupported-loop";
    case LoopVectorizationError::UnsupportedBody:
        return "unsupported-body";
    case LoopVectorizationError::UnsupportedAccess:
        return "unsupported-access";
    case LoopVectorizationError::UnsupportedType:
        return "unsupported-type";
    }
    return "unknown";
}

} // namespace hira::polyhedral
