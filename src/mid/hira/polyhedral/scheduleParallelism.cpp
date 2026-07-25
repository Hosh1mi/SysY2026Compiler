#include "../../../include/mid/hira/polyhedral/scheduleParallelism.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace hira::polyhedral {
namespace {

bool containsDimension(const PolyhedralStatement &statement,
                       AffineVariable dimension) {
    return std::find(statement.dimensions.begin(),
                     statement.dimensions.end(),
                     dimension) != statement.dimensions.end();
}

std::optional<AffineVariable> directEqualDimension(
    const AffineEquality &equality) {
    if (equality.source.constantTerm() !=
        equality.sink.constantTerm())
        return std::nullopt;

    std::map<AffineVariable, std::int64_t> sourceTerms;
    std::map<AffineVariable, std::int64_t> sinkTerms;
    for (const auto &[variable, coefficient] :
         equality.source.coefficients())
        sourceTerms[variable] += coefficient;
    for (const auto &[variable, coefficient] :
         equality.sink.coefficients())
        sinkTerms[variable] += coefficient;
    if (sourceTerms != sinkTerms ||
        sourceTerms.size() != 1)
        return std::nullopt;
    const auto &[variable, coefficient] =
        *sourceTerms.begin();
    if (!coefficient ||
        variable.kind != AffineVariableKind::Dimension)
        return std::nullopt;
    return variable;
}

bool provesEqualIteration(const DependenceRelation &relation,
                          AffineVariable dimension) {
    for (const DimensionDistance &distance :
         relation.dimensionDistances)
        if (distance.source == dimension &&
            distance.sink == dimension)
            return distance.distance == 0;

    if (relation.precision != DependencePrecision::Exact &&
        relation.precision !=
            DependencePrecision::ConservativeDomain)
        return false;
    for (const AffineEquality &equality :
         relation.accessEqualities)
        if (directEqualDimension(equality) == dimension)
            return true;
    return false;
}

std::optional<AffineVariable> rootDimension(
    const PolyhedralModel &model) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimensions.size() == 1)
            return domain.dimension;
    return std::nullopt;
}

AffineVariable scheduledDimension(
    AffineVariable dimension,
    const ScheduleCandidate &candidate) {
    for (std::size_t index = 0;
         index < candidate.originalDimensions.size();
         ++index)
        if (dimension ==
            candidate.originalDimensions[index])
            return candidate.scheduledDimensions[index];
    return dimension;
}

std::vector<DependenceId> blockersFor(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    AffineVariable dimension) {
    std::vector<DependenceId> blockers;
    for (std::size_t index = 0;
         index < dependences.relations().size(); ++index) {
        if (feasibility.relations()[index].kind ==
            DependenceFeasibilityKind::ProvenEmpty)
            continue;
        const DependenceRelation &relation =
            dependences.relations()[index];
        if (!relation.sourceStatement ||
            !relation.sinkStatement)
            continue;
        const PolyhedralStatement &source =
            model.statements()[*relation.sourceStatement];
        const PolyhedralStatement &sink =
            model.statements()[*relation.sinkStatement];
        if (!containsDimension(source, dimension) ||
            !containsDimension(sink, dimension))
            continue;
        if (!provesEqualIteration(relation, dimension))
            blockers.push_back(relation.id);
    }
    return blockers;
}

bool same(const ScheduleParallelism &left,
          const ScheduleParallelism &right) {
    return left.schedule == right.schedule &&
           left.outerDimension == right.outerDimension &&
           left.outerParallel == right.outerParallel &&
           left.blockers == right.blockers;
}

std::string dimensionName(AffineVariable dimension) {
    return std::string(
               dimension.kind == AffineVariableKind::Dimension
                   ? "d"
                   : "s") +
           std::to_string(dimension.position);
}

} // namespace

ScheduleParallelismResult analyzeScheduleParallelism(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules) {
    ScheduleParallelismResult result;
    std::optional<AffineVariable> root =
        rootDimension(model);
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        ScheduleParallelism parallelism;
        parallelism.schedule = candidate.id;
        if (root) {
            parallelism.outerDimension =
                scheduledDimension(*root, candidate);
            parallelism.blockers = blockersFor(
                model, dependences, feasibility,
                *parallelism.outerDimension);
            parallelism.outerParallel =
                parallelism.blockers.empty();
        }
        result.schedules_.push_back(
            std::move(parallelism));
    }
    return result;
}

bool verifyScheduleParallelism(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &result,
    std::string &detail) {
    ScheduleParallelismResult expected =
        analyzeScheduleParallelism(
            model, dependences, feasibility, schedules);
    if (result.schedules().size() !=
        expected.schedules().size()) {
        detail = "incomplete-schedule-parallelism";
        return false;
    }
    for (std::size_t index = 0;
         index < result.schedules().size(); ++index)
        if (!same(result.schedules()[index],
                  expected.schedules()[index])) {
            detail = "invalid-schedule-parallelism";
            return false;
        }
    return true;
}

std::string printScheduleParallelism(
    const ScheduleParallelismResult &result) {
    std::ostringstream out;
    out << "polyhedral.schedule_parallelism {\n";
    for (const ScheduleParallelism &parallelism :
         result.schedules()) {
        out << "  C" << parallelism.schedule << " outer=";
        if (parallelism.outerDimension)
            out << dimensionName(
                *parallelism.outerDimension);
        else
            out << "<none>";
        out << " = "
            << (parallelism.outerParallel
                    ? "parallel"
                    : "sequential");
        if (!parallelism.blockers.empty()) {
            out << " blockers=[";
            for (std::size_t index = 0;
                 index < parallelism.blockers.size(); ++index) {
                if (index)
                    out << ", ";
                out << "D" << parallelism.blockers[index];
            }
            out << "]";
        }
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
