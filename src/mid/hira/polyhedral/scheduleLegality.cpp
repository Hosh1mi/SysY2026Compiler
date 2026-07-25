#include "../../../include/mid/hira/polyhedral/scheduleLegality.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
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

bool sameSchedule(const std::vector<ScheduleComponent> &left,
                  const std::vector<ScheduleComponent> &right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!sameComponent(left[index], right[index]))
            return false;
    return true;
}

const StatementSchedule *statementSchedule(
    const ScheduleCandidate &candidate,
    StatementId statement) {
    if (statement >= candidate.statements.size() ||
        candidate.statements[statement].statement != statement)
        return nullptr;
    return &candidate.statements[statement];
}

bool isIdentityCandidate(const PolyhedralModel &model,
                         const ScheduleCandidate &candidate) {
    if (candidate.statements.size() !=
        model.statements().size())
        return false;
    for (const PolyhedralStatement &statement :
         model.statements())
        if (!sameSchedule(
                candidate.statements[statement.id].components,
                statement.identitySchedule))
            return false;
    return true;
}

struct KnownDistances {
    std::map<DimensionPair, std::int64_t> values;
    bool conflict = false;
};

void addDistance(KnownDistances &distances,
                 DimensionPair dimensions,
                 std::int64_t distance) {
    auto [position, inserted] =
        distances.values.emplace(dimensions, distance);
    if (!inserted && position->second != distance)
        distances.conflict = true;
}

std::optional<std::pair<DimensionPair, std::int64_t>>
distanceFromEquality(const AffineEquality &equality) {
    std::map<AffineVariable, std::int64_t> sourceSymbols;
    std::map<AffineVariable, std::int64_t> sinkSymbols;
    std::vector<std::pair<AffineVariable, std::int64_t>>
        sourceDimensions;
    std::vector<std::pair<AffineVariable, std::int64_t>>
        sinkDimensions;
    for (const auto &term : equality.source.coefficients())
        if (term.first.kind == AffineVariableKind::Dimension)
            sourceDimensions.push_back(term);
        else
            sourceSymbols.insert(term);
    for (const auto &term : equality.sink.coefficients())
        if (term.first.kind == AffineVariableKind::Dimension)
            sinkDimensions.push_back(term);
        else
            sinkSymbols.insert(term);

    if (sourceSymbols != sinkSymbols ||
        sourceDimensions.size() != 1 ||
        sinkDimensions.size() != 1 ||
        sourceDimensions.front().second !=
            sinkDimensions.front().second)
        return std::nullopt;

    std::int64_t coefficient =
        sourceDimensions.front().second;
    __int128 difference =
        static_cast<__int128>(
            equality.source.constantTerm()) -
        static_cast<__int128>(
            equality.sink.constantTerm());
    if (!coefficient || difference % coefficient)
        return std::nullopt;
    __int128 distance = difference / coefficient;
    if (distance < std::numeric_limits<std::int64_t>::min() ||
        distance > std::numeric_limits<std::int64_t>::max())
        return std::nullopt;
    return std::make_pair(
        DimensionPair{sourceDimensions.front().first,
                      sinkDimensions.front().first},
        static_cast<std::int64_t>(distance));
}

KnownDistances knownDistances(
    const DependenceRelation &relation) {
    KnownDistances distances;
    for (const DimensionDistance &distance :
         relation.dimensionDistances)
        addDistance(distances,
                    {distance.source, distance.sink},
                    distance.distance);
    if (relation.precision == DependencePrecision::Exact ||
        relation.precision ==
            DependencePrecision::ConservativeDomain)
        for (const AffineEquality &equality :
             relation.accessEqualities)
            if (auto distance = distanceFromEquality(equality))
                addDistance(distances, distance->first,
                            distance->second);
    return distances;
}

ScheduledDependenceStatus compareSchedulePair(
    const StatementSchedule &source,
    const StatementSchedule &sink,
    const KnownDistances &distances) {
    if (distances.conflict)
        return ScheduledDependenceStatus::Unknown;

    std::size_t common =
        std::min(source.components.size(),
                 sink.components.size());
    for (std::size_t index = 0; index < common; ++index) {
        const ScheduleComponent &left =
            source.components[index];
        const ScheduleComponent &right =
            sink.components[index];
        if (left.kind != right.kind)
            return ScheduledDependenceStatus::Unknown;
        if (left.kind == ScheduleComponentKind::Iteration) {
            auto distance = distances.values.find(
                {left.dimension, right.dimension});
            if (distance == distances.values.end())
                return ScheduledDependenceStatus::Unknown;
            if (distance->second > 0)
                return ScheduledDependenceStatus::Preserved;
            if (distance->second < 0)
                return ScheduledDependenceStatus::Violated;
            continue;
        }
        if (left.position < right.position)
            return ScheduledDependenceStatus::Preserved;
        if (left.position > right.position)
            return ScheduledDependenceStatus::Violated;
    }
    if (source.components.size() != sink.components.size())
        return ScheduledDependenceStatus::Unknown;
    return ScheduledDependenceStatus::Violated;
}

bool preservesIdentityOrderModuloEqualDimensions(
    const StatementSchedule &identitySource,
    const StatementSchedule &identitySink,
    const StatementSchedule &candidateSource,
    const StatementSchedule &candidateSink,
    const KnownDistances &distances) {
    if (distances.conflict ||
        identitySource.components.size() !=
            identitySink.components.size() ||
        identitySource.components.size() !=
            candidateSource.components.size() ||
        identitySink.components.size() !=
            candidateSink.components.size())
        return false;

    using Pair = std::pair<AffineVariable, AffineVariable>;
    std::vector<Pair> identityUnequal;
    std::vector<Pair> candidateUnequal;
    auto collect = [&](const StatementSchedule &source,
                       const StatementSchedule &sink,
                       std::vector<Pair> &unequal) {
        for (std::size_t index = 0;
             index < source.components.size(); ++index) {
            const ScheduleComponent &left =
                source.components[index];
            const ScheduleComponent &right =
                sink.components[index];
            if (left.kind != right.kind)
                return false;
            if (left.kind !=
                ScheduleComponentKind::Iteration)
                continue;
            Pair dimensions{left.dimension, right.dimension};
            auto distance = distances.values.find(dimensions);
            if (distance != distances.values.end()) {
                if (distance->second != 0)
                    return false;
                continue;
            }
            unequal.push_back(dimensions);
        }
        return true;
    };

    if (!collect(identitySource, identitySink,
                 identityUnequal) ||
        !collect(candidateSource, candidateSink,
                 candidateUnequal) ||
        identityUnequal != candidateUnequal)
        return false;

    // Schedule construction may permute iteration components only.  Requiring
    // all static sequence/branch components to remain in their original slots
    // ensures that the same within-iteration tie breaker is used after every
    // dimension proven equal above.
    for (std::size_t index = 0;
         index < identitySource.components.size(); ++index) {
        const ScheduleComponent &sourceIdentity =
            identitySource.components[index];
        const ScheduleComponent &sinkIdentity =
            identitySink.components[index];
        const ScheduleComponent &sourceCandidate =
            candidateSource.components[index];
        const ScheduleComponent &sinkCandidate =
            candidateSink.components[index];
        if (sourceIdentity.kind != sourceCandidate.kind ||
            sinkIdentity.kind != sinkCandidate.kind)
            return false;
        if (sourceIdentity.kind !=
                ScheduleComponentKind::Iteration &&
            (sourceIdentity.position != sourceCandidate.position ||
             sinkIdentity.position != sinkCandidate.position))
            return false;
    }
    return true;
}

const char *dependenceStatusName(
    ScheduledDependenceStatus status) {
    switch (status) {
    case ScheduledDependenceStatus::Preserved:
        return "preserved";
    case ScheduledDependenceStatus::Violated:
        return "violated";
    case ScheduledDependenceStatus::Unknown:
        return "unknown";
    case ScheduledDependenceStatus::Irrelevant:
        return "irrelevant";
    }
    return "unknown";
}

const char *legalityName(ScheduleLegalityKind kind) {
    switch (kind) {
    case ScheduleLegalityKind::Legal:
        return "legal";
    case ScheduleLegalityKind::Illegal:
        return "illegal";
    case ScheduleLegalityKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace

ScheduleLegalityResult analyzeScheduleLegality(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules) {
    ScheduleLegalityResult result;
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        ScheduleLegality legality;
        legality.schedule = candidate.id;
        legality.kind = ScheduleLegalityKind::Legal;
        bool identity = isIdentityCandidate(model, candidate);

        for (std::size_t index = 0;
             index < dependences.relations().size(); ++index) {
            const DependenceRelation &relation =
                dependences.relations()[index];
            const DependenceFeasibility &relationFeasibility =
                feasibility.relations()[index];
            ScheduledDependence scheduled;
            scheduled.dependence = relation.id;
            if (relationFeasibility.kind ==
                DependenceFeasibilityKind::ProvenEmpty) {
                scheduled.status =
                    ScheduledDependenceStatus::Irrelevant;
            } else if (identity) {
                scheduled.status =
                    ScheduledDependenceStatus::Preserved;
            } else if (!relation.sourceStatement ||
                       !relation.sinkStatement) {
                scheduled.status =
                    ScheduledDependenceStatus::Unknown;
            } else {
                const StatementSchedule *source =
                    statementSchedule(
                        candidate, *relation.sourceStatement);
                const StatementSchedule *sink =
                    statementSchedule(
                        candidate, *relation.sinkStatement);
                if (!source || !sink) {
                    scheduled.status =
                        ScheduledDependenceStatus::Unknown;
                } else if (
                    sameSchedule(
                        source->components,
                        model.statements()[
                            *relation.sourceStatement]
                            .identitySchedule) &&
                    sameSchedule(
                        sink->components,
                        model.statements()[
                            *relation.sinkStatement]
                            .identitySchedule)) {
                    scheduled.status =
                        ScheduledDependenceStatus::Preserved;
                } else {
                    KnownDistances distances =
                        knownDistances(relation);
                    scheduled.status = compareSchedulePair(
                        *source, *sink, distances);
                    if (scheduled.status ==
                            ScheduledDependenceStatus::Unknown &&
                        relation.ordering ==
                            DependenceOrdering::IdentityBefore &&
                        preservesIdentityOrderModuloEqualDimensions(
                            {relation.sourceStatement.value(),
                             model.statements()[
                                 *relation.sourceStatement]
                                 .identitySchedule},
                            {relation.sinkStatement.value(),
                             model.statements()[
                                 *relation.sinkStatement]
                                 .identitySchedule},
                            *source, *sink, distances))
                        scheduled.status =
                            ScheduledDependenceStatus::Preserved;
                    if (scheduled.status ==
                            ScheduledDependenceStatus::Violated &&
                        relationFeasibility.kind !=
                            DependenceFeasibilityKind::Required)
                        scheduled.status =
                            ScheduledDependenceStatus::Unknown;
                }
            }
            if (scheduled.status ==
                ScheduledDependenceStatus::Violated)
                legality.kind = ScheduleLegalityKind::Illegal;
            else if (scheduled.status ==
                         ScheduledDependenceStatus::Unknown &&
                     legality.kind ==
                         ScheduleLegalityKind::Legal)
                legality.kind = ScheduleLegalityKind::Unknown;
            legality.dependences.push_back(scheduled);
        }
        result.schedules_.push_back(std::move(legality));
    }
    return result;
}

bool verifyScheduleLegality(
    const DependenceSet &dependences,
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &result,
    std::string &detail) {
    if (result.schedules().size() !=
        schedules.candidates().size()) {
        detail = "incomplete-schedule-legality";
        return false;
    }
    for (std::size_t scheduleIndex = 0;
         scheduleIndex < result.schedules().size();
         ++scheduleIndex) {
        const ScheduleLegality &legality =
            result.schedules()[scheduleIndex];
        if (legality.schedule != scheduleIndex ||
            legality.dependences.size() !=
                dependences.relations().size()) {
            detail = "invalid-schedule-legality";
            return false;
        }
        bool violated = false;
        bool unknown = false;
        for (std::size_t dependenceIndex = 0;
             dependenceIndex < legality.dependences.size();
             ++dependenceIndex) {
            const ScheduledDependence &dependence =
                legality.dependences[dependenceIndex];
            if (dependence.dependence != dependenceIndex) {
                detail = "invalid-scheduled-dependence";
                return false;
            }
            violated |=
                dependence.status ==
                ScheduledDependenceStatus::Violated;
            unknown |=
                dependence.status ==
                ScheduledDependenceStatus::Unknown;
        }
        ScheduleLegalityKind expected =
            violated ? ScheduleLegalityKind::Illegal
                     : unknown ? ScheduleLegalityKind::Unknown
                               : ScheduleLegalityKind::Legal;
        if (legality.kind != expected) {
            detail = "mismatched-schedule-legality";
            return false;
        }
    }
    return true;
}

std::string printScheduleLegality(
    const ScheduleLegalityResult &result) {
    std::ostringstream out;
    out << "polyhedral.schedule_legality {\n";
    for (const ScheduleLegality &schedule :
         result.schedules()) {
        out << "  C" << schedule.schedule << " = "
            << legalityName(schedule.kind) << " [";
        for (std::size_t index = 0;
             index < schedule.dependences.size(); ++index) {
            if (index)
                out << ", ";
            const ScheduledDependence &dependence =
                schedule.dependences[index];
            out << "D" << dependence.dependence << ":"
                << dependenceStatusName(dependence.status);
        }
        out << "]\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
