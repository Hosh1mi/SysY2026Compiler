#include "../../../include/mid/hira/transform/bandLeadingDistribution.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace hira::polyhedral {
namespace {

const IterationDomain *domainFor(const PolyhedralModel &model,
                                 const HiraLoop *loop) {
    for (const IterationDomain &domain : model.domains())
        if (domain.loop == loop)
            return &domain;
    return nullptr;
}

const PolyhedralStatement *statementFor(
    const PolyhedralModel &model, const HiraNode *node) {
    for (const PolyhedralStatement &statement :
         model.statements())
        if (statement.node == node)
            return &statement;
    return nullptr;
}

std::optional<std::size_t> innerLoopIndex(
    const HiraLoop &outer, const HiraLoop &inner) {
    if (inner.parent() != &outer.body())
        return std::nullopt;
    const auto &nodes = outer.body().nodes();
    for (std::size_t index = 0; index < nodes.size();
         ++index)
        if (nodes[index].get() == &inner)
            return index;
    return std::nullopt;
}

bool containsNestedLoop(const HiraSequence &sequence) {
    for (const auto &owner : sequence.nodes())
        if (dynamic_cast<const HiraLoop *>(owner.get()))
            return true;
    return false;
}

bool statementUsesDimension(
    const PolyhedralStatement &statement,
    AffineVariable dimension) {
    return std::find(statement.dimensions.begin(),
                     statement.dimensions.end(),
                     dimension) != statement.dimensions.end();
}

} // namespace

bool leadingPayloadBlocksInterchange(
    const PolyhedralModel &model, const HiraLoop &outer,
    const HiraLoop &inner) {
    if (isPerfectLoopNest(outer, inner))
        return false;
    const IterationDomain *innerDomain =
        domainFor(model, &inner);
    if (!innerDomain)
        return false;
    auto innerIndex = innerLoopIndex(outer, inner);
    if (!innerIndex || *innerIndex == 0)
        return true;
    const auto &nodes = outer.body().nodes();
    if (nodes.size() < *innerIndex + 3)
        return true;
    for (std::size_t index = 0; index < *innerIndex; ++index) {
        const HiraNode *node = nodes[index].get();
        if (dynamic_cast<const HiraLoop *>(node) ||
            dynamic_cast<const HiraIf *>(node))
            return true;
        const PolyhedralStatement *statement =
            statementFor(model, node);
        if (!statement)
            return true;
        if (statementUsesDimension(*statement,
                                   innerDomain->dimension))
            return true;
    }
    return false;
}

bool distributeLeadingPayload(
    HiraRegion &region, const PolyhedralModel &model,
    HiraLoop &outer, HiraLoop &inner) {
    if (leadingPayloadBlocksInterchange(model, outer, inner))
        return false;
    if (isPerfectLoopNest(outer, inner))
        return true;
    auto innerIndex = innerLoopIndex(outer, inner);
    if (!innerIndex || *innerIndex == 0)
        return false;

    std::vector<std::unique_ptr<HiraNode>> moved;
    moved.reserve(*innerIndex);
    for (std::size_t index = 0; index < *innerIndex; ++index)
        moved.push_back(
            outer.body().remove(outer.body().nodes().front().get()));

    for (std::size_t index = 0; index < moved.size(); ++index)
        inner.body().insert(index, std::move(moved[index]));

    region.markModified();
    return isPerfectLoopNest(outer, inner);
}

bool loopsAreParentChild(const HiraLoop &outer,
                         const HiraLoop &inner) {
    return inner.parent() == &outer.body();
}

const IterationDomain *findDomain(
    const PolyhedralModel &model,
    AffineVariable dimension) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimension == dimension)
            return &domain;
    return nullptr;
}

bool trailingPayloadBlocksInterchange(
    const PolyhedralModel &model, const HiraLoop &outer,
    const HiraLoop &inner) {
    if (isPerfectLoopNest(outer, inner))
        return false;
    const IterationDomain *outerDomain =
        domainFor(model, &outer);
    const IterationDomain *innerDomain =
        domainFor(model, &inner);
    if (!innerDomain)
        return false;
    auto innerIndex = innerLoopIndex(outer, inner);
    if (!innerIndex)
        return true;
    const auto &nodes = outer.body().nodes();
    if (nodes.size() <= *innerIndex + 1)
        return false;
    for (std::size_t index = *innerIndex + 1;
         index < nodes.size(); ++index) {
        const HiraNode *node = nodes[index].get();
        if (dynamic_cast<const HiraLoop *>(node) ||
            dynamic_cast<const HiraIf *>(node))
            return true;
        const PolyhedralStatement *statement =
            statementFor(model, node);
        if (!statement)
            return true;
        if (statementUsesDimension(*statement,
                                   innerDomain->dimension))
            return true;
        if (outerDomain &&
            statementUsesDimension(*statement,
                                   outerDomain->dimension))
            return true;
    }
    return false;
}

bool distributeTrailingPayload(
    HiraRegion &region, const PolyhedralModel &model,
    HiraLoop &outer, HiraLoop &inner) {
    if (trailingPayloadBlocksInterchange(model, outer, inner))
        return false;
    if (isPerfectLoopNest(outer, inner))
        return true;
    auto innerIndex = innerLoopIndex(outer, inner);
    if (!innerIndex)
        return false;

    if (outer.body().nodes().size() <= *innerIndex + 1)
        return false;

    std::vector<std::unique_ptr<HiraNode>> moved;
    while (outer.body().nodes().size() > *innerIndex + 1) {
        HiraNode *node = outer.body().nodes()[*innerIndex + 1].get();
        moved.push_back(outer.body().remove(node));
    }

    for (auto &node : moved)
        inner.body().append(std::move(node));

    region.markModified();
    return isPerfectLoopNest(outer, inner);
}

bool normalizeBandForPermutation(
    HiraRegion &region, const PolyhedralModel &model,
    const std::vector<AffineVariable> &bandDimensions) {
    if (bandDimensions.size() < 2)
        return true;
    bool changed = false;
    for (std::size_t index = 1;
         index < bandDimensions.size(); ++index) {
        const IterationDomain *firstDomain =
            findDomain(model, bandDimensions[index - 1]);
        const IterationDomain *secondDomain =
            findDomain(model, bandDimensions[index]);
        if (!firstDomain || !secondDomain || !firstDomain->loop ||
            !secondDomain->loop)
            return false;
        HiraLoop *first =
            const_cast<HiraLoop *>(firstDomain->loop);
        HiraLoop *second =
            const_cast<HiraLoop *>(secondDomain->loop);
        if (loopsAreParentChild(*first, *second)) {
            if (!isPerfectLoopNest(*first, *second) &&
                distributeLeadingPayload(
                    region, model, *first, *second))
                changed = true;
            if (!isPerfectLoopNest(*first, *second) &&
                distributeTrailingPayload(
                    region, model, *first, *second))
                changed = true;
        } else if (loopsAreParentChild(*second, *first)) {
            if (!isPerfectLoopNest(*second, *first) &&
                distributeLeadingPayload(
                    region, model, *second, *first))
                changed = true;
            if (!isPerfectLoopNest(*second, *first) &&
                distributeTrailingPayload(
                    region, model, *second, *first))
                changed = true;
        }
    }
    (void)changed;
    return true;
}

} // namespace hira::polyhedral
