#include "../../../include/mid/hira/polyhedral/scheduleVerifier.hpp"

#include <optional>
#include <set>
#include <utility>

namespace hira::polyhedral {
namespace {

using DimensionPair =
    std::pair<AffineVariable, AffineVariable>;

bool sameComponent(const ScheduleComponent &left,
                   const ScheduleComponent &right) {
    return left.kind == right.kind &&
           left.position == right.position &&
           (left.kind != ScheduleComponentKind::Iteration ||
            left.dimension == right.dimension);
}

bool isNestedPair(const PolyhedralModel &model,
                  DimensionPair dimensions) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimensions.size() >= 2 &&
            domain.dimensions[domain.dimensions.size() - 2] ==
                dimensions.first &&
            domain.dimensions.back() == dimensions.second)
            return true;
    return false;
}

bool contains(
    const std::vector<ScheduleComponent> &components,
    AffineVariable dimension) {
    for (const ScheduleComponent &component : components)
        if (component.kind == ScheduleComponentKind::Iteration &&
            component.dimension == dimension)
            return true;
    return false;
}

AffineVariable expectedDimension(
    AffineVariable original,
    const std::vector<ScheduleComponent> &identity,
    std::optional<DimensionPair> interchange) {
    if (!interchange ||
        !contains(identity, interchange->first) ||
        !contains(identity, interchange->second))
        return original;
    if (original == interchange->first)
        return interchange->second;
    if (original == interchange->second)
        return interchange->first;
    return original;
}

} // namespace

bool verifyScheduleCandidates(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    std::string &detail) {
    if (schedules.candidates().empty()) {
        detail = "missing-identity-schedule";
        return false;
    }
    for (std::size_t candidateIndex = 0;
         candidateIndex < schedules.candidates().size();
         ++candidateIndex) {
        const ScheduleCandidate &candidate =
            schedules.candidates()[candidateIndex];
        if (candidate.id != candidateIndex ||
            candidate.statements.size() !=
                model.statements().size()) {
            detail = "invalid-schedule-candidate";
            return false;
        }
        if ((candidateIndex == 0) !=
            (candidate.kind ==
             ScheduleCandidateKind::Identity)) {
            detail = "invalid-identity-schedule";
            return false;
        }

        std::optional<DimensionPair> interchange;
        if (candidate.kind ==
            ScheduleCandidateKind::Interchange) {
            interchange = {
                candidate.outerDimension,
                candidate.innerDimension};
            if (candidate.outerDimension.kind !=
                    AffineVariableKind::Dimension ||
                candidate.innerDimension.kind !=
                    AffineVariableKind::Dimension ||
                candidate.outerDimension ==
                    candidate.innerDimension ||
                !model.space().source(
                    candidate.outerDimension) ||
                !model.space().source(
                    candidate.innerDimension) ||
                !isNestedPair(model, *interchange)) {
                detail = "invalid-interchange-dimensions";
                return false;
            }
        }

        for (const PolyhedralStatement &statement :
             model.statements()) {
            const StatementSchedule &scheduled =
                candidate.statements[statement.id];
            if (scheduled.statement != statement.id ||
                scheduled.components.size() !=
                    statement.identitySchedule.size()) {
                detail = "invalid-statement-schedule";
                return false;
            }

            std::multiset<AffineVariable> dimensions;
            for (std::size_t componentIndex = 0;
                 componentIndex < scheduled.components.size();
                 ++componentIndex) {
                const ScheduleComponent &original =
                    statement.identitySchedule[componentIndex];
                const ScheduleComponent &component =
                    scheduled.components[componentIndex];
                if (original.kind ==
                    ScheduleComponentKind::Iteration) {
                    if (component.kind !=
                            ScheduleComponentKind::Iteration ||
                        !(component.dimension ==
                          expectedDimension(
                              original.dimension,
                              statement.identitySchedule,
                              interchange))) {
                        detail = "invalid-iteration-component";
                        return false;
                    }
                    dimensions.insert(component.dimension);
                } else if (!sameComponent(original, component)) {
                    detail = "modified-static-component";
                    return false;
                }
            }
            if (dimensions !=
                std::multiset<AffineVariable>(
                    statement.dimensions.begin(),
                    statement.dimensions.end())) {
                detail = "invalid-dimension-permutation";
                return false;
            }
        }
    }
    return true;
}

} // namespace hira::polyhedral
