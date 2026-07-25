#include "../../../include/mid/hira/transform/scheduleRealization.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

ScheduleRealizationResult reject(
    ScheduleCandidateId schedule,
    ScheduleRealizationError error, std::string detail) {
    ScheduleRealizationResult result;
    result.schedule = schedule;
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

const IterationDomain *findDomain(
    const PolyhedralModel &model, const HiraLoop *loop) {
    for (const IterationDomain &domain : model.domains())
        if (domain.loop == loop)
            return &domain;
    return nullptr;
}

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    const auto &nodes = sequence.nodes();
    for (std::size_t index = 0; index < nodes.size(); ++index)
        if (nodes[index].get() == node)
            return index;
    return std::nullopt;
}

bool hasMovableNestShape(
    const HiraLoop &outer, const HiraLoop &inner) {
    const auto &innerBody = inner.body().nodes();
    return outer.parent() && isPerfectLoopNest(outer, inner) &&
           innerBody.size() >= 2 &&
           analyzeCanonicalLoopControl(outer) &&
           analyzeCanonicalLoopControl(inner);
}

std::vector<const HiraValue *> dimensionSources(
    const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions) {
    std::vector<const HiraValue *> result;
    result.reserve(dimensions.size());
    for (AffineVariable dimension : dimensions)
        result.push_back(model.space().source(dimension));
    return result;
}

bool sameSemanticSchedule(
    const PolyhedralModel &expectedModel,
    const std::vector<ScheduleComponent> &expected,
    const PolyhedralModel &actualModel,
    const std::vector<ScheduleComponent> &actual) {
    if (expected.size() != actual.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const ScheduleComponent &left = expected[index];
        const ScheduleComponent &right = actual[index];
        if (left.kind != right.kind ||
            (left.kind != ScheduleComponentKind::Iteration &&
             left.position != right.position) ||
            (left.kind == ScheduleComponentKind::Iteration &&
             expectedModel.space().source(left.dimension) !=
                 actualModel.space().source(right.dimension)))
            return false;
    }
    return true;
}

const PolyhedralStatement *findStatement(
    const PolyhedralModel &model, const HiraNode *node) {
    for (const PolyhedralStatement &statement :
         model.statements())
        if (statement.node == node)
            return &statement;
    return nullptr;
}

const AccessRelation *findAccess(
    const PolyhedralModel &model, const HiraNode *node) {
    const PolyhedralStatement *statement =
        findStatement(model, node);
    if (!statement)
        return nullptr;
    for (const AccessRelation &access : model.accesses())
        if (access.statement == statement->id)
            return &access;
    return nullptr;
}

std::optional<AffineVariable> innermostDimension(
    const ScheduleCandidate &candidate,
    StatementId statement) {
    if (statement >= candidate.statements.size() ||
        candidate.statements[statement].statement != statement)
        return std::nullopt;
    const auto &components =
        candidate.statements[statement].components;
    for (auto component = components.rbegin();
         component != components.rend(); ++component)
        if (component->kind ==
            ScheduleComponentKind::Iteration)
            return component->dimension;
    return std::nullopt;
}

} // namespace

ScheduleRealizationResult realizeSelectedSchedule(
    HiraRegion &region, const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleSelectionResult &selection) {
    const ScheduleCandidateId selected = selection.selected();
    if (selected >= schedules.candidates().size())
        return reject(
            selected,
            ScheduleRealizationError::InvalidSelection,
            "selected-schedule-out-of-range");

    const ScheduleCandidate &candidate =
        schedules.candidates()[selected];
    if (candidate.id != selected)
        return reject(
            selected,
            ScheduleRealizationError::InvalidSelection,
            "selected-schedule-id-mismatch");
    if (candidate.kind == ScheduleCandidateKind::Identity)
        return {false, selected,
                ScheduleRealizationError::None, {}};
    if (candidate.kind != ScheduleCandidateKind::Interchange)
        return reject(
            selected,
            ScheduleRealizationError::UnsupportedCandidate,
            "unsupported-schedule-kind");

    const IterationDomain *outerDomain =
        findDomain(model, candidate.outerDimension);
    const IterationDomain *innerDomain =
        findDomain(model, candidate.innerDimension);
    if (!outerDomain || !innerDomain ||
        !outerDomain->loop || !innerDomain->loop)
        return reject(
            selected,
            ScheduleRealizationError::MissingLoopDomain,
            "missing-interchange-loop");

    auto *outer = const_cast<HiraLoop *>(outerDomain->loop);
    auto *inner = const_cast<HiraLoop *>(innerDomain->loop);
    if (!hasMovableNestShape(*outer, *inner))
        return reject(
            selected, ScheduleRealizationError::InvalidNest,
            "interchange-nest-shape-changed");

    HiraSequence *parent = outer->parent();
    auto outerPosition = nodePosition(*parent, outer);
    if (!outerPosition)
        return reject(
            selected, ScheduleRealizationError::InvalidNest,
            "outer-loop-owner-mismatch");

    const std::size_t payloadCount =
        inner->body().nodes().size() - 2;
    std::unique_ptr<HiraNode> outerOwner =
        parent->remove(outer);
    std::unique_ptr<HiraNode> innerOwner =
        outer->body().remove(inner);

    std::vector<std::unique_ptr<HiraNode>> payload;
    payload.reserve(payloadCount);
    for (std::size_t index = 0; index < payloadCount; ++index)
        payload.push_back(inner->body().remove(
            inner->body().nodes().front().get()));

    for (std::size_t index = 0; index < payload.size(); ++index)
        outer->body().insert(index, std::move(payload[index]));
    inner->body().insert(0, std::move(outerOwner));
    parent->insert(*outerPosition, std::move(innerOwner));

    region.markModified();
    return {true, selected,
            ScheduleRealizationError::None, {}};
}

bool verifyScheduleRealization(
    const PolyhedralModel &before,
    const ScheduleCandidate &candidate,
    const PolyhedralModel &after, std::string &detail) {
    if (candidate.kind != ScheduleCandidateKind::Interchange) {
        detail = "non-interchange-realization";
        return false;
    }

    const IterationDomain *beforeOuter =
        findDomain(before, candidate.outerDimension);
    const IterationDomain *beforeInner =
        findDomain(before, candidate.innerDimension);
    if (!beforeOuter || !beforeInner ||
        !beforeOuter->loop || !beforeInner->loop) {
        detail = "missing-original-loop-domain";
        return false;
    }
    const IterationDomain *afterOuter =
        findDomain(after, beforeOuter->loop);
    const IterationDomain *afterInner =
        findDomain(after, beforeInner->loop);
    if (!afterOuter || !afterInner) {
        detail = "missing-realized-loop-domain";
        return false;
    }

    std::vector<const HiraValue *> prefix =
        dimensionSources(before, beforeOuter->dimensions);
    if (prefix.empty() ||
        prefix.back() != beforeOuter->loop->induction()) {
        detail = "invalid-original-outer-domain";
        return false;
    }
    prefix.pop_back();

    std::vector<const HiraValue *> expectedNewOuter = prefix;
    expectedNewOuter.push_back(
        beforeInner->loop->induction());
    std::vector<const HiraValue *> expectedNewInner =
        expectedNewOuter;
    expectedNewInner.push_back(
        beforeOuter->loop->induction());
    if (dimensionSources(after, afterInner->dimensions) !=
            expectedNewOuter ||
        dimensionSources(after, afterOuter->dimensions) !=
            expectedNewInner) {
        detail = "realized-loop-order-mismatch";
        return false;
    }

    if (candidate.statements.size() !=
            before.statements().size() ||
        before.statements().size() !=
            after.statements().size()) {
        detail = "realized-statement-count-mismatch";
        return false;
    }
    for (const PolyhedralStatement &statement :
         before.statements()) {
        if (statement.id >= candidate.statements.size()) {
            detail = "invalid-selected-statement";
            return false;
        }
        const StatementSchedule &expected =
            candidate.statements[statement.id];
        const PolyhedralStatement *realized =
            findStatement(after, statement.node);
        if (expected.statement != statement.id || !realized) {
            detail = "missing-realized-statement";
            return false;
        }
        if (!sameSemanticSchedule(
                before, expected.components, after,
                realized->identitySchedule)) {
            detail = "realized-statement-schedule-mismatch";
            return false;
        }
    }

    if (before.accesses().size() != after.accesses().size()) {
        detail = "realized-access-count-mismatch";
        return false;
    }
    for (const AccessRelation &access : before.accesses()) {
        if (access.statement >= before.statements().size()) {
            detail = "invalid-original-access";
            return false;
        }
        const HiraNode *node =
            before.statements()[access.statement].node;
        const AccessRelation *realized =
            findAccess(after, node);
        const PolyhedralStatement *realizedStatement =
            findStatement(after, node);
        auto expectedDimension =
            innermostDimension(candidate, access.statement);
        if (!realized || !realizedStatement ||
            !expectedDimension ||
            realizedStatement->dimensions.empty()) {
            detail = "missing-realized-access";
            return false;
        }

        AffineVariable actualDimension =
            realizedStatement->dimensions.back();
        if (before.space().source(*expectedDimension) !=
            after.space().source(actualDimension)) {
            detail = "realized-access-order-mismatch";
            return false;
        }
        auto expectedStride = analyzeLinearAccessStride(
            before, access, *expectedDimension);
        auto actualStride = analyzeLinearAccessStride(
            after, *realized, actualDimension);
        if (!expectedStride || !actualStride ||
            *expectedStride != *actualStride) {
            detail = "realized-access-stride-mismatch";
            return false;
        }
    }
    return true;
}

const char *scheduleRealizationErrorName(
    ScheduleRealizationError error) {
    switch (error) {
    case ScheduleRealizationError::None:
        return "none";
    case ScheduleRealizationError::InvalidSelection:
        return "invalid-selection";
    case ScheduleRealizationError::UnsupportedCandidate:
        return "unsupported-candidate";
    case ScheduleRealizationError::MissingLoopDomain:
        return "missing-loop-domain";
    case ScheduleRealizationError::InvalidNest:
        return "invalid-nest";
    }
    return "unknown";
}

} // namespace hira::polyhedral
