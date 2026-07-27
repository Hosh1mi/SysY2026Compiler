#include "../../../include/mid/hira/transform/loopTiling.hpp"

#include "../../../include/mid/hira/analysis/loopInterchangeAnalysis.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/basicBlock.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"

#include <algorithm>
#include <limits>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

LoopTilingResult reject(LoopTilingError error,
                        std::string detail) {
    LoopTilingResult result;
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
    std::initializer_list<HiraValue *> operands,
    int predicate = 0) {
    auto compute =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        compute->addOperand(operand);
    compute->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.append(std::move(compute)));
}

bool interchangeAdjacent(
    const PolyhedralModel &model, HiraLoop &outer,
    HiraLoop &inner) {
    auto nest =
        analyzeAdjacentLoopInterchange(
            model, outer, inner);
    if (!nest)
        return false;
    HiraSequence *parent = outer.parent();
    auto outerPosition = nodePosition(*parent, &outer);
    if (!outerPosition)
        return false;

    const std::size_t payloadCount =
        inner.body().nodes().size() - 2;
    std::unique_ptr<HiraNode> outerOwner =
        parent->remove(&outer);
    std::unique_ptr<HiraNode> innerOwner =
        nest->innerSequence->remove(&inner);
    std::vector<std::unique_ptr<HiraNode>> payload;
    payload.reserve(payloadCount);
    for (std::size_t index = 0;
         index < payloadCount; ++index)
        payload.push_back(inner.body().remove(
            inner.body().nodes().front().get()));
    for (std::size_t index = 0;
         index < payload.size(); ++index)
        nest->innerSequence->insert(
            nest->innerPosition + index,
            std::move(payload[index]));
    inner.body().insert(0, std::move(outerOwner));
    parent->insert(*outerPosition, std::move(innerOwner));
    return true;
}

} // namespace

LoopTilingResult stripMineLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t tileSize) {
    if (tileSize <= 1 ||
        tileSize >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()))
        return reject(LoopTilingError::InvalidTileSize,
                      "tile-size-out-of-range");

    const IterationDomain *domain =
        findDomain(model, dimension);
    if (!domain || !domain->loop)
        return reject(LoopTilingError::MissingLoopDomain,
                      "missing-point-loop");
    auto *pointLoop =
        const_cast<HiraLoop *>(domain->loop);
    if (!pointLoop->step() ||
        pointLoop->step()->kind() !=
            ValueKind::IntegerConstant ||
        pointLoop->step()->integerValue() != 1)
        return reject(LoopTilingError::NonUnitPointLoop,
                      "point-loop-step");
    HiraSequence *parent = pointLoop->parent();
    if (!parent)
        return reject(LoopTilingError::InvalidNest,
                      "point-loop-without-parent");
    auto position = nodePosition(*parent, pointLoop);
    if (!position)
        return reject(LoopTilingError::InvalidNest,
                      "point-loop-owner-mismatch");

    Loop *sourceLoop =
        region.sourceMapping().sourceLoop(pointLoop);
    Module *module =
        sourceLoop && sourceLoop->header &&
                sourceLoop->header->parent_
            ? sourceLoop->header->parent_->parent_
            : nullptr;
    if (!sourceLoop || !module)
        return reject(LoopTilingError::InvalidNest,
                      "missing-source-loop");

    Type *indexType = pointLoop->induction()->type();
    HiraValue *oldLower = pointLoop->lowerBound();
    HiraValue *oldUpper = pointLoop->upperBound();
    HiraValue *tileStep =
        region.createIntegerConstant(indexType, tileSize);
    HiraValue *tileInduction =
        region.createValue(indexType);
    auto tileOwner = std::make_unique<HiraLoop>(
        tileInduction, oldLower, oldUpper, tileStep);
    HiraLoop *tileLoop = tileOwner.get();
    region.sourceMapping().mapLoop(tileLoop, sourceLoop);

    struct TiledCarried {
        HiraValue *pointResult = nullptr;
    };
    std::vector<TiledCarried> tiledCarried;
    tiledCarried.reserve(
        pointLoop->carriedValues().size());
    for (std::size_t index = 0;
         index < pointLoop->carriedValues().size();
         ++index) {
        HiraLoop::CarriedBinding binding =
            pointLoop->carriedValues()[index];
        HiraValue *pointResult =
            region.createValue(binding.result->type());
        pointLoop->setCarriedResult(index, pointResult);
        HiraValue *tileIteration =
            region.createValue(binding.iteration->type());
        tileLoop->addCarriedValue(
            binding.initial, tileIteration,
            binding.result);
        pointLoop->setCarriedInitial(
            index, tileIteration);
        tiledCarried.push_back({pointResult});
    }

    std::unique_ptr<HiraNode> pointOwner =
        parent->remove(pointLoop);
    HiraSequence &tileBody = tileLoop->body();

    HiraValue *tileEnd = region.createValue(indexType);
    appendCompute(tileBody, ComputeKind::Add, tileEnd,
                  {tileInduction, tileStep});
    HiraValue *withinUpper =
        region.createValue(module->int1_ty_);
    appendCompute(
        tileBody, ComputeKind::ICmp, withinUpper,
        {tileEnd, oldUpper}, ICmpInst::ICMP_SLT);
    HiraValue *pointUpper =
        region.createValue(indexType);
    appendCompute(tileBody, ComputeKind::Select,
                  pointUpper,
                  {withinUpper, tileEnd, oldUpper});

    pointLoop->setLowerBound(tileInduction);
    pointLoop->setUpperBound(pointUpper);
    tileBody.append(std::move(pointOwner));

    HiraValue *tileNext = region.createValue(indexType);
    appendCompute(tileBody, ComputeKind::Add, tileNext,
                  {tileInduction, tileStep});
    tileLoop->addYieldValue(tileNext);
    for (std::size_t index = 0;
         index < tiledCarried.size(); ++index) {
        tileLoop->setCarriedYield(
            index, tiledCarried[index].pointResult);
        tileLoop->addYieldValue(
            tiledCarried[index].pointResult);
    }
    auto yield = std::make_unique<HiraYield>();
    yield->addOperand(tileNext);
    for (const TiledCarried &binding : tiledCarried)
        yield->addOperand(binding.pointResult);
    tileBody.append(std::move(yield));

    parent->insert(*position, std::move(tileOwner));
    region.markModified();
    return {true, LoopTilingError::None, {}};
}

LoopTilingResult tileLoopBand(
    HiraRegion &region, const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions,
    const std::vector<std::uint32_t> &tileSizes) {
    if (dimensions.size() != tileSizes.size() ||
        dimensions.empty())
        return reject(LoopTilingError::InvalidTileSize,
                      "invalid-band-tile-shape");

    std::vector<HiraLoop *> pointLoops;
    pointLoops.reserve(dimensions.size());
    std::size_t firstTiled = dimensions.size();
    std::size_t tiledCount = 0;
    for (std::size_t index = 0;
         index < dimensions.size(); ++index) {
        const IterationDomain *domain =
            findDomain(model, dimensions[index]);
        if (!domain || !domain->loop)
            return reject(
                LoopTilingError::MissingLoopDomain,
                "missing-band-loop");
        auto *loop =
            const_cast<HiraLoop *>(domain->loop);
        if (!loop->step() ||
            loop->step()->kind() !=
                ValueKind::IntegerConstant ||
            loop->step()->integerValue() != 1)
            return reject(
                LoopTilingError::NonUnitPointLoop,
                "non-unit-band-loop");
        if (!loop->carriedValues().empty() &&
            index + 1 != dimensions.size())
            return reject(
                LoopTilingError::LoopCarriedState,
                "non-innermost-band-carried-state");
        pointLoops.push_back(loop);
        if (tileSizes[index] > 1) {
            firstTiled = std::min(firstTiled, index);
            ++tiledCount;
        }
    }
    if (tiledCount < 2)
        return reject(LoopTilingError::InvalidTileSize,
                      "band-needs-multiple-tiled-dimensions");
    for (std::size_t index = firstTiled + 1;
         index < pointLoops.size(); ++index) {
        if (!pointLoops[index]->carriedValues().empty()) {
            continue;
        }
        if (!analyzeAdjacentLoopInterchange(
                model, *pointLoops[index - 1],
                *pointLoops[index]))
            return reject(
                LoopTilingError::InvalidNest,
                "unsafe-imperfect-band");
    }

    std::vector<HiraLoop *> tileLoops(
        dimensions.size(), nullptr);
    for (std::size_t index = firstTiled;
         index < dimensions.size(); ++index) {
        if (tileSizes[index] <= 1)
            continue;
        HiraLoop *point = pointLoops[index];
        HiraSequence *parent = point->parent();
        auto position =
            parent ? nodePosition(*parent, point)
                   : std::nullopt;
        LoopTilingResult tiled = stripMineLoop(
            region, model, dimensions[index],
            tileSizes[index]);
        if (!tiled.succeeded())
            return tiled;
        if (!parent || !position)
            return reject(LoopTilingError::InvalidNest,
                          "missing-created-tile-loop");
        tileLoops[index] =
            dynamic_cast<HiraLoop *>(
                parent->nodes()[*position].get());
        if (!tileLoops[index])
            return reject(LoopTilingError::InvalidNest,
                          "created-tile-is-not-loop");
    }

    std::vector<HiraLoop *> current;
    for (std::size_t index = 0;
         index < dimensions.size(); ++index) {
        if (tileLoops[index])
            current.push_back(tileLoops[index]);
        current.push_back(pointLoops[index]);
    }
    std::vector<HiraLoop *> desired;
    for (std::size_t index = 0; index < firstTiled; ++index)
        desired.push_back(pointLoops[index]);
    for (std::size_t index = firstTiled;
         index < dimensions.size(); ++index)
        if (tileLoops[index] &&
            pointLoops[index]->carriedValues().empty())
            desired.push_back(tileLoops[index]);
    for (std::size_t index = firstTiled;
         index < dimensions.size(); ++index) {
        desired.push_back(pointLoops[index]);
        if (!pointLoops[index]->carriedValues().empty() &&
            tileLoops[index]) {
            desired.pop_back();
            desired.push_back(tileLoops[index]);
            desired.push_back(pointLoops[index]);
        }
    }

    for (std::size_t target = 0;
         target < desired.size(); ++target) {
        auto position = std::find(
            current.begin() + target, current.end(),
            desired[target]);
        if (position == current.end())
            return reject(LoopTilingError::InvalidNest,
                          "invalid-tiled-band-order");
        std::size_t currentPosition =
            static_cast<std::size_t>(
                position - current.begin());
        while (currentPosition > target) {
            if (!interchangeAdjacent(
                    model,
                    *current[currentPosition - 1],
                    *current[currentPosition]))
                return reject(
                    LoopTilingError::InvalidNest,
                    "cannot-order-tiled-band");
            std::swap(current[currentPosition - 1],
                      current[currentPosition]);
            --currentPosition;
        }
    }
    region.markModified();
    return {true, LoopTilingError::None, {}};
}

const char *loopTilingErrorName(LoopTilingError error) {
    switch (error) {
    case LoopTilingError::None:
        return "none";
    case LoopTilingError::InvalidTileSize:
        return "invalid-tile-size";
    case LoopTilingError::MissingLoopDomain:
        return "missing-loop-domain";
    case LoopTilingError::NonUnitPointLoop:
        return "non-unit-point-loop";
    case LoopTilingError::LoopCarriedState:
        return "loop-carried-state";
    case LoopTilingError::InvalidNest:
        return "invalid-nest";
    }
    return "unknown";
}

} // namespace hira::polyhedral
