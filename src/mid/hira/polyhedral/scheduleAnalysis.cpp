#include "../../../include/mid/hira/polyhedral/scheduleAnalysis.hpp"

#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace hira::polyhedral {
namespace {

using DimensionPair =
    std::pair<AffineVariable, AffineVariable>;

std::string variableName(AffineVariable variable) {
    return std::string(
               variable.kind == AffineVariableKind::Dimension
                   ? "d"
                   : "s") +
           std::to_string(variable.position);
}

void printSchedule(
    std::ostringstream &out,
    const std::vector<ScheduleComponent> &components) {
    out << "[";
    for (std::size_t index = 0; index < components.size();
         ++index) {
        if (index)
            out << ", ";
        const ScheduleComponent &component = components[index];
        switch (component.kind) {
        case ScheduleComponentKind::SequencePosition:
            out << "seq" << component.position;
            break;
        case ScheduleComponentKind::Iteration:
            out << variableName(component.dimension);
            break;
        case ScheduleComponentKind::Branch:
            out << (component.position ? "else" : "then");
            break;
        }
    }
    out << "]";
}

StatementSchedule makeStatementSchedule(
    const PolyhedralStatement &statement,
    std::optional<DimensionPair> interchange) {
    StatementSchedule schedule;
    schedule.statement = statement.id;
    schedule.components = statement.identitySchedule;
    if (!interchange)
        return schedule;

    std::optional<std::size_t> outer;
    std::optional<std::size_t> inner;
    for (std::size_t index = 0;
         index < schedule.components.size(); ++index) {
        ScheduleComponent &component =
            schedule.components[index];
        if (component.kind !=
            ScheduleComponentKind::Iteration)
            continue;
        if (component.dimension == interchange->first)
            outer = index;
        else if (component.dimension == interchange->second)
            inner = index;
    }
    if (outer && inner)
        std::swap(schedule.components[*outer].dimension,
                  schedule.components[*inner].dimension);
    return schedule;
}

ScheduleCandidate makeCandidate(
    ScheduleCandidateId id, ScheduleCandidateKind kind,
    const PolyhedralModel &model,
    std::optional<DimensionPair> interchange = std::nullopt) {
    ScheduleCandidate candidate;
    candidate.id = id;
    candidate.kind = kind;
    if (interchange) {
        candidate.outerDimension = interchange->first;
        candidate.innerDimension = interchange->second;
    }
    for (const PolyhedralStatement &statement :
         model.statements())
        candidate.statements.push_back(
            makeStatementSchedule(statement, interchange));
    return candidate;
}

const char *candidateKindName(ScheduleCandidateKind kind) {
    switch (kind) {
    case ScheduleCandidateKind::Identity:
        return "identity";
    case ScheduleCandidateKind::Interchange:
        return "interchange";
    }
    return "unknown";
}

} // namespace

ScheduleCandidateSet
buildScheduleCandidates(const PolyhedralModel &model) {
    ScheduleCandidateSet result;
    result.candidates_.push_back(
        makeCandidate(0, ScheduleCandidateKind::Identity, model));

    std::set<DimensionPair> interchanges;
    for (const IterationDomain &domain : model.domains()) {
        if (domain.dimensions.size() < 2)
            continue;
        interchanges.insert(
            {domain.dimensions[domain.dimensions.size() - 2],
             domain.dimensions.back()});
    }
    for (const DimensionPair &interchange : interchanges) {
        ScheduleCandidateId id =
            static_cast<ScheduleCandidateId>(
                result.candidates_.size());
        result.candidates_.push_back(makeCandidate(
            id, ScheduleCandidateKind::Interchange, model,
            interchange));
    }
    return result;
}

std::string printScheduleCandidates(
    const ScheduleCandidateSet &schedules) {
    std::ostringstream out;
    out << "polyhedral.schedules {\n";
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        out << "  C" << candidate.id << " "
            << candidateKindName(candidate.kind);
        if (candidate.kind ==
            ScheduleCandidateKind::Interchange)
            out << "(" << variableName(candidate.outerDimension)
                << ", "
                << variableName(candidate.innerDimension) << ")";
        out << " {\n";
        for (const StatementSchedule &statement :
             candidate.statements) {
            out << "    S" << statement.statement << " -> ";
            printSchedule(out, statement.components);
            out << "\n";
        }
        out << "  }\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
