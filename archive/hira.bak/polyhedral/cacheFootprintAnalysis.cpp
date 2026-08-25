#include "../../../include/mid/hira/polyhedral/cacheFootprintAnalysis.hpp"

#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace hira::polyhedral {
namespace {

using WideUInt = unsigned __int128;

std::vector<AffineVariable> largestBand(
    const ScheduleCandidate &candidate) {
    std::vector<AffineVariable> dimensions;
    for (const ScheduleTreeNode &node :
         candidate.tree.nodes())
        if (node.kind == ScheduleTreeNodeKind::Band &&
            node.band.dimensions.size() > dimensions.size())
            dimensions = node.band.dimensions;
    return dimensions;
}

std::optional<std::uint64_t> footprintFor(
    const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions,
    const std::vector<std::uint32_t> &tileSizes) {
    if (dimensions.size() != tileSizes.size())
        return std::nullopt;

    std::map<MemoryObjectId, WideUInt> objectSpans;
    for (const AccessRelation &access : model.accesses()) {
        auto elementSize =
            analyzeAccessElementSize(model, access);
        if (!elementSize || *elementSize < 0)
            return std::nullopt;
        WideUInt span =
            static_cast<WideUInt>(*elementSize);
        for (std::size_t index = 0;
             index < dimensions.size(); ++index) {
            auto stride = analyzeLinearAccessStride(
                model, access, dimensions[index]);
            if (!stride)
                return std::nullopt;
            span += static_cast<WideUInt>(
                        tileSizes[index] - 1) *
                    static_cast<WideUInt>(*stride);
        }
        auto &objectSpan = objectSpans[access.object];
        objectSpan = std::max(objectSpan, span);
    }

    WideUInt total = 0;
    for (const auto &[object, span] : objectSpans) {
        (void)object;
        total += span;
    }
    if (total >
        std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return static_cast<std::uint64_t>(total);
}

const PolyhedralStatement *findStatement(
    const PolyhedralModel &model, StatementId id) {
    for (const PolyhedralStatement &statement :
         model.statements())
        if (statement.id == id)
            return &statement;
    return nullptr;
}

std::set<MemoryObjectId> reusableObjects(
    const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions,
    const std::vector<std::uint32_t> *activeTileSizes = nullptr) {
    std::set<MemoryObjectId> result;
    for (const AccessRelation &access : model.accesses()) {
        const PolyhedralStatement *statement =
            findStatement(model, access.statement);
        if (!statement)
            continue;
        for (std::size_t index = 0;
             index < dimensions.size(); ++index) {
            AffineVariable dimension = dimensions[index];
            if (activeTileSizes &&
                (*activeTileSizes)[index] <= 1)
                continue;
            // An invariant subscript is temporal reuse only when the
            // statement actually executes inside that dimension.  A load
            // already hoisted outside a loop must not make its whole object
            // look resident across that loop.
            if (std::find(statement->dimensions.begin(),
                          statement->dimensions.end(),
                          dimension) ==
                statement->dimensions.end())
                continue;
            auto stride = analyzeLinearAccessStride(
                model, access, dimension);
            if (stride && *stride == 0) {
                result.insert(access.object);
                break;
            }
        }
    }
    return result;
}

std::optional<std::uint64_t> reusableFootprintFor(
    const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions,
    const std::vector<std::uint32_t> &tileSizes,
    const std::set<MemoryObjectId> &reusable) {
    if (dimensions.size() != tileSizes.size())
        return std::nullopt;

    std::map<MemoryObjectId, WideUInt> objectSpans;
    for (const AccessRelation &access : model.accesses()) {
        if (!reusable.count(access.object))
            continue;
        auto elementSize =
            analyzeAccessElementSize(model, access);
        if (!elementSize || *elementSize < 0)
            return std::nullopt;
        WideUInt span =
            static_cast<WideUInt>(*elementSize);
        for (std::size_t index = 0;
             index < dimensions.size(); ++index) {
            auto stride = analyzeLinearAccessStride(
                model, access, dimensions[index]);
            if (!stride)
                return std::nullopt;
            span += static_cast<WideUInt>(
                        tileSizes[index] - 1) *
                    static_cast<WideUInt>(*stride);
        }
        auto &objectSpan = objectSpans[access.object];
        objectSpan = std::max(objectSpan, span);
    }

    WideUInt total = 0;
    for (const auto &[object, span] : objectSpans) {
        (void)object;
        total += span;
    }
    if (total >
        std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return static_cast<std::uint64_t>(total);
}

std::optional<std::uint32_t> constantTripCount(
    const PolyhedralModel &model,
    AffineVariable dimension) {
    for (const IterationDomain &domain : model.domains()) {
        if (!(domain.dimension == dimension) || !domain.loop)
            continue;
        const HiraValue *lower = domain.loop->lowerBound();
        const HiraValue *upper = domain.loop->upperBound();
        const HiraValue *step = domain.loop->step();
        if (!lower || !upper || !step ||
            lower->kind() != ValueKind::IntegerConstant ||
            upper->kind() != ValueKind::IntegerConstant ||
            step->kind() != ValueKind::IntegerConstant ||
            step->integerValue() != 1)
            return std::nullopt;
        std::int64_t trip =
            upper->integerValue() - lower->integerValue();
        if (trip <= 0 ||
            static_cast<std::uint64_t>(trip) >
                std::numeric_limits<std::uint32_t>::max())
            return std::nullopt;
        return static_cast<std::uint32_t>(trip);
    }
    return std::nullopt;
}

bool reusableWorkingSetAlreadyFits(
    const PolyhedralModel &model,
    const target::A53TargetModel &target,
    CacheFootprint &result) {
    std::set<MemoryObjectId> reusable =
        reusableObjects(model, result.dimensions);
    if (reusable.empty()) {
        result.kind = CacheFootprintKind::Known;
        result.l1FootprintBytes = 0;
        result.tileVolume = 1;
        result.tileSizes.assign(result.dimensions.size(), 1);
        return true;
    }

    std::vector<std::uint32_t> fullSizes;
    fullSizes.reserve(result.dimensions.size());
    for (AffineVariable dimension : result.dimensions) {
        auto trip = constantTripCount(model, dimension);
        if (!trip)
            return false;
        fullSizes.push_back(*trip);
    }
    auto footprint = reusableFootprintFor(
        model, result.dimensions, fullSizes, reusable);
    if (!footprint ||
        *footprint > target.l1UsableBytes)
        return false;

    result.kind = CacheFootprintKind::Known;
    result.l1FootprintBytes = *footprint;
    result.tileVolume = 1;
    result.tileSizes.assign(result.dimensions.size(), 1);
    return true;
}

bool capturedReusableWorkingSetAlreadyFits(
    const PolyhedralModel &model,
    const target::A53TargetModel &target,
    CacheFootprint &result) {
    std::set<MemoryObjectId> captured =
        reusableObjects(model, result.dimensions,
                        &result.tileSizes);
    std::vector<std::uint32_t> fullSizes;
    fullSizes.reserve(result.dimensions.size());
    for (AffineVariable dimension : result.dimensions) {
        auto trip = constantTripCount(model, dimension);
        if (!trip)
            return false;
        fullSizes.push_back(*trip);
    }
    auto footprint = reusableFootprintFor(
        model, result.dimensions, fullSizes, captured);
    if (!footprint ||
        *footprint > target.l1UsableBytes)
        return false;

    result.kind = CacheFootprintKind::Known;
    result.l1FootprintBytes = *footprint;
    result.tileVolume = 1;
    result.tileSizes.assign(result.dimensions.size(), 1);
    return true;
}

std::uint64_t tileVolume(
    const std::vector<std::uint32_t> &sizes) {
    WideUInt volume = 1;
    for (std::uint32_t size : sizes)
        volume *= size;
    if (volume >
        std::numeric_limits<std::uint64_t>::max())
        return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(volume);
}

void searchTiles(
    const PolyhedralModel &model,
    const target::A53TargetModel &target,
    CacheFootprint &result, std::size_t position,
    std::vector<std::uint32_t> &current) {
    static constexpr std::uint32_t choices[] = {
        1, 4, 8, 16, 32, 64,
    };
    if (position == current.size()) {
        auto footprint = footprintFor(
            model, result.dimensions, current);
        if (!footprint ||
            *footprint > target.l1UsableBytes)
            return;
        std::uint64_t volume = tileVolume(current);
        if (volume > result.tileVolume ||
            (volume == result.tileVolume &&
             *footprint < result.l1FootprintBytes)) {
            result.kind = CacheFootprintKind::Known;
            result.l1FootprintBytes = *footprint;
            result.tileVolume = volume;
            result.tileSizes = current;
        }
        return;
    }
    for (std::uint32_t choice : choices) {
        if (position == 0 && choice > 1) {
            const IterationDomain *domain = nullptr;
            for (const IterationDomain &candidate :
                 model.domains())
                if (candidate.dimension ==
                    result.dimensions[position]) {
                    domain = &candidate;
                    break;
                }
            const HiraLoop *loop =
                domain ? domain->loop : nullptr;
            const HiraValue *lower =
                loop ? loop->lowerBound() : nullptr;
            const HiraValue *upper =
                loop ? loop->upperBound() : nullptr;
            if (!lower || !upper ||
                lower->kind() !=
                    ValueKind::IntegerConstant ||
                upper->kind() !=
                    ValueKind::IntegerConstant)
                continue;
            std::int64_t trip =
                upper->integerValue() -
                lower->integerValue();
            std::uint64_t requiredTiles =
                static_cast<std::uint64_t>(
                    target.evaluationWorkers) *
                target.minimumParallelTilesPerWorker;
            if (trip <= 0 ||
                static_cast<std::uint64_t>(trip) <
                    static_cast<std::uint64_t>(choice) *
                        requiredTiles)
                continue;
        }
        current[position] = choice;
        searchTiles(model, target, result,
                    position + 1, current);
    }
}

bool same(const CacheFootprint &left,
          const CacheFootprint &right) {
    return left.schedule == right.schedule &&
           left.kind == right.kind &&
           left.l1FootprintBytes ==
               right.l1FootprintBytes &&
           left.tileVolume == right.tileVolume &&
           left.dimensions == right.dimensions &&
           left.tileSizes == right.tileSizes;
}

std::string dimensionName(AffineVariable dimension) {
    return std::string(
               dimension.kind ==
                       AffineVariableKind::Dimension
                   ? "d"
                   : "s") +
           std::to_string(dimension.position);
}

} // namespace

CacheFootprintResult analyzeCacheFootprints(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target) {
    CacheFootprintResult result;
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        CacheFootprint footprint;
        footprint.schedule = candidate.id;
        std::vector<AffineVariable> band =
            largestBand(candidate);
        const std::size_t tiledDimensions =
            std::min<std::size_t>(3, band.size());
        footprint.dimensions.assign(
            band.end() - tiledDimensions, band.end());
        std::vector<std::uint32_t> current(
            footprint.dimensions.size(), 1);
        if (model.accesses().empty()) {
            footprint.kind = CacheFootprintKind::Known;
            footprint.tileSizes = current;
            footprint.tileVolume = 1;
        } else if (reusableWorkingSetAlreadyFits(
                       model, target, footprint)) {
            // Keeping a streaming array's whole affine span in L1 is neither
            // necessary nor possible.  If every object with proven temporal
            // reuse already fits, strip-mining only adds control overhead and
            // inhibits downstream unrolling/vectorization.
        } else {
            searchTiles(model, target, footprint, 0,
                        current);
            if (footprint.kind ==
                    CacheFootprintKind::Known)
                capturedReusableWorkingSetAlreadyFits(
                    model, target, footprint);
        }
        result.schedules_.push_back(
            std::move(footprint));
    }
    return result;
}

bool verifyCacheFootprints(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target,
    const CacheFootprintResult &result,
    std::string &detail) {
    CacheFootprintResult expected =
        analyzeCacheFootprints(model, schedules, target);
    if (result.schedules().size() !=
        expected.schedules().size()) {
        detail = "incomplete-cache-footprints";
        return false;
    }
    for (std::size_t index = 0;
         index < result.schedules().size(); ++index)
        if (!same(result.schedules()[index],
                  expected.schedules()[index])) {
            detail = "invalid-cache-footprint";
            return false;
        }
    return true;
}

std::string printCacheFootprints(
    const CacheFootprintResult &result) {
    std::ostringstream out;
    out << "polyhedral.cache_footprints target=a53 {\n";
    for (const CacheFootprint &footprint :
         result.schedules()) {
        out << "  C" << footprint.schedule << " = ";
        if (footprint.kind ==
            CacheFootprintKind::Unknown) {
            out << "unknown\n";
            continue;
        }
        out << footprint.l1FootprintBytes
            << "B volume=" << footprint.tileVolume
            << " tile=[";
        for (std::size_t index = 0;
             index < footprint.dimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << dimensionName(
                       footprint.dimensions[index])
                << ":" << footprint.tileSizes[index];
        }
        out << "]\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
