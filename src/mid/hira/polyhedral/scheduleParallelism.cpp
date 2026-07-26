#include "../../../include/mid/hira/polyhedral/scheduleParallelism.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"

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

// A recurrence-carried dependence does not block band parallelism when
// the recurrence is an exact reduction carried by that band and the
// dependence's sink is the reduction update itself: privatization gives
// each worker a private accumulator for it.  Any other read of the
// running value stays a blocker.
bool isExemptReductionSelfLoop(
    const PolyhedralModel &model,
    const ReductionAnalysisResult &reductions,
    const DependenceRelation &relation,
    AffineVariable dimension) {
    if (relation.kind != DependenceKind::RecurrenceCarried ||
        !relation.sourceRecurrence)
        return false;
    const ScalarRecurrence *recurrence = nullptr;
    for (const ScalarRecurrence &candidate :
         model.scalarRecurrences())
        if (candidate.id == *relation.sourceRecurrence) {
            recurrence = &candidate;
            break;
        }
    if (!recurrence || !(recurrence->dimension == dimension))
        return false;
    bool exact = false;
    for (const ScalarReduction &reduction :
         reductions.scalarReductions())
        if (reduction.recurrence == recurrence->id &&
            reduction.parallelSemantics ==
                ReductionParallelSemantics::Exact)
            exact = true;
    if (!exact)
        return false;
    const HiraNode *update =
        recurrence->yielded
            ? recurrence->yielded->definingNode()
            : nullptr;
    if (!update || !relation.sinkStatement)
        return false;
    return model.statements()[*relation.sinkStatement].node ==
           update;
}

bool isTaskPrivateMemoryDependence(
    const PolyhedralModel &model,
    const DependenceRelation &relation) {
    if (!relation.sourceAccess || !relation.sinkAccess ||
        *relation.sourceAccess >= model.accesses().size() ||
        *relation.sinkAccess >= model.accesses().size())
        return false;
    const AccessRelation &source =
        model.accesses()[*relation.sourceAccess];
    const AccessRelation &sink =
        model.accesses()[*relation.sinkAccess];
    return source.object == sink.object &&
           source.object < model.memoryObjects().size() &&
           model.memoryObjects()[source.object].taskPrivate;
}

std::vector<DependenceId> blockersFor(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ReductionAnalysisResult &reductions,
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
        if (isExemptReductionSelfLoop(model, reductions,
                                      relation, dimension))
            continue;
        if (isTaskPrivateMemoryDependence(model, relation))
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
    const ReductionAnalysisResult &reductions,
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
                model, dependences, feasibility, reductions,
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
    const ReductionAnalysisResult &reductions,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &result,
    std::string &detail) {
    ScheduleParallelismResult expected =
        analyzeScheduleParallelism(
            model, dependences, feasibility, reductions,
            schedules);
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
