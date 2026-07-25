#include "../../../include/mid/hira/polyhedral/scheduleAnalysis.hpp"
#include "../../../include/mid/hira/target/a53TargetModel.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace hira::polyhedral {
namespace {

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

std::vector<AffineVariable> iterationDimensions(
    const StatementSchedule &statement);

StatementSchedule makeStatementSchedule(
    const PolyhedralStatement &statement,
    const std::vector<AffineVariable> &original,
    const std::vector<AffineVariable> &scheduled) {
    StatementSchedule schedule;
    schedule.statement = statement.id;
    schedule.components = statement.identitySchedule;
    if (original.empty() ||
        original.size() != scheduled.size())
        return schedule;

    for (AffineVariable dimension : original)
        if (std::find(statement.dimensions.begin(),
                      statement.dimensions.end(),
                      dimension) ==
            statement.dimensions.end())
            return schedule;

    for (std::size_t index = 0;
         index < schedule.components.size(); ++index) {
        ScheduleComponent &component =
            schedule.components[index];
        if (component.kind !=
            ScheduleComponentKind::Iteration)
            continue;
        auto position = std::find(
            original.begin(), original.end(),
            component.dimension);
        if (position != original.end())
            component.dimension = scheduled[
                static_cast<std::size_t>(
                    position - original.begin())];
    }
    return schedule;
}

ScheduleCandidate makeCandidate(
    ScheduleCandidateId id, ScheduleCandidateKind kind,
    const PolyhedralModel &model,
    std::vector<AffineVariable> original = {},
    std::vector<AffineVariable> scheduled = {}) {
    ScheduleCandidate candidate;
    candidate.id = id;
    candidate.kind = kind;
    candidate.originalDimensions = std::move(original);
    candidate.scheduledDimensions = std::move(scheduled);
    if (kind == ScheduleCandidateKind::Interchange) {
        std::size_t changed = 0;
        for (std::size_t index = 0;
             index < candidate.originalDimensions.size();
             ++index)
            if (!(candidate.originalDimensions[index] ==
                  candidate.scheduledDimensions[index])) {
                if (changed++ == 0)
                    candidate.outerDimension =
                        candidate.originalDimensions[index];
                else
                    candidate.innerDimension =
                        candidate.originalDimensions[index];
            }
    }
    for (const PolyhedralStatement &statement :
         model.statements())
        candidate.statements.push_back(
            makeStatementSchedule(
                statement, candidate.originalDimensions,
                candidate.scheduledDimensions));
    candidate.tree = buildScheduleTree(candidate.statements);
    return candidate;
}

bool isSingleInterchange(
    const std::vector<AffineVariable> &original,
    const std::vector<AffineVariable> &scheduled) {
    std::vector<std::size_t> changed;
    for (std::size_t index = 0;
         index < original.size(); ++index)
        if (!(original[index] == scheduled[index]))
            changed.push_back(index);
    return changed.size() == 2 &&
           changed[1] == changed[0] + 1 &&
           original[changed[0]] ==
               scheduled[changed[1]] &&
           original[changed[1]] ==
               scheduled[changed[0]];
}

using ScheduleKey =
    std::vector<std::vector<AffineVariable>>;

ScheduleKey keyFor(const ScheduleCandidate &candidate) {
    ScheduleKey key;
    key.reserve(candidate.statements.size());
    for (const StatementSchedule &statement :
         candidate.statements)
        key.push_back(iterationDimensions(statement));
    return key;
}

const char *candidateKindName(ScheduleCandidateKind kind) {
    switch (kind) {
    case ScheduleCandidateKind::Identity:
        return "identity";
    case ScheduleCandidateKind::Interchange:
        return "interchange";
    case ScheduleCandidateKind::Permutation:
        return "permutation";
    }
    return "unknown";
}

const char *treeNodeKindName(ScheduleTreeNodeKind kind) {
    switch (kind) {
    case ScheduleTreeNodeKind::Sequence:
        return "sequence";
    case ScheduleTreeNodeKind::Band:
        return "band";
    case ScheduleTreeNodeKind::Filter:
        return "filter";
    case ScheduleTreeNodeKind::Guard:
        return "guard";
    }
    return "unknown";
}

std::vector<AffineVariable> iterationDimensions(
    const StatementSchedule &statement) {
    std::vector<AffineVariable> dimensions;
    for (const ScheduleComponent &component :
         statement.components)
        if (component.kind ==
            ScheduleComponentKind::Iteration)
            dimensions.push_back(component.dimension);
    return dimensions;
}

} // namespace

ScheduleTree buildScheduleTree(
    const std::vector<StatementSchedule> &statements) {
    ScheduleTree tree;
    auto appendNode = [&](ScheduleTreeNodeKind kind,
                          std::optional<ScheduleTreeNodeId> parent) {
        ScheduleTreeNodeId id =
            static_cast<ScheduleTreeNodeId>(
                tree.nodes_.size());
        tree.nodes_.push_back({});
        ScheduleTreeNode &node = tree.nodes_.back();
        node.id = id;
        node.kind = kind;
        node.parent = parent;
        if (parent)
            tree.nodes_[*parent].children.push_back(id);
        return id;
    };
    tree.root_ = appendNode(
        ScheduleTreeNodeKind::Sequence, std::nullopt);

    // A candidate still carries the explicit schedule of every statement.
    // The tree groups statements with the same surrounding band, providing a
    // stable representation for later distribution, fusion and tiling.
    std::map<std::vector<AffineVariable>,
             std::vector<StatementId>>
        groups;
    for (const StatementSchedule &statement : statements)
        groups[iterationDimensions(statement)].push_back(
            statement.statement);

    for (const auto &[dimensions, statementIds] : groups) {
        ScheduleTreeNodeId bandId = appendNode(
            ScheduleTreeNodeKind::Band, tree.root_);
        ScheduleTreeNode &band = tree.nodes_[bandId];
        band.band.dimensions = dimensions;
        band.band.coincident.assign(dimensions.size(), false);
        band.band.tileSizes.assign(dimensions.size(), 0);
        band.band.permutable = dimensions.size() > 1;

        ScheduleTreeNodeId filterId = appendNode(
            ScheduleTreeNodeKind::Filter, bandId);
        tree.nodes_[filterId].statements = statementIds;
    }
    return tree;
}

ScheduleCandidateSet
buildScheduleCandidates(const PolyhedralModel &model) {
    ScheduleCandidateSet result;
    result.candidates_.push_back(
        makeCandidate(0, ScheduleCandidateKind::Identity, model));

    std::set<std::vector<AffineVariable>> bands;
    for (const IterationDomain &domain : model.domains()) {
        if (domain.dimensions.size() < 2)
            continue;
        bands.insert(domain.dimensions);
    }

    std::set<ScheduleKey> emitted;
    emitted.insert(keyFor(result.candidates_.front()));
    const std::size_t stateLimit =
        target::cortexA53().maxScheduleStates;
    for (const auto &band : bands) {
        std::vector<std::size_t> order(band.size());
        std::iota(order.begin(), order.end(), 0);
        while (std::next_permutation(order.begin(),
                                     order.end())) {
            std::vector<AffineVariable> scheduled;
            scheduled.reserve(band.size());
            for (std::size_t index : order)
                scheduled.push_back(band[index]);
            ScheduleCandidateKind kind =
                isSingleInterchange(band, scheduled)
                    ? ScheduleCandidateKind::Interchange
                    : ScheduleCandidateKind::Permutation;
            ScheduleCandidate candidate = makeCandidate(
                static_cast<ScheduleCandidateId>(
                    result.candidates_.size()),
                kind, model, band, scheduled);
            if (!emitted.insert(keyFor(candidate)).second)
                continue;
            result.candidates_.push_back(
                std::move(candidate));
            if (result.candidates_.size() >= stateLimit)
                return result;
        }
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
        if (candidate.kind !=
            ScheduleCandidateKind::Identity) {
            out << "([";
            for (std::size_t index = 0;
                 index < candidate.originalDimensions.size();
                 ++index) {
                if (index)
                    out << ", ";
                out << variableName(
                    candidate.originalDimensions[index]);
            }
            out << "] -> [";
            for (std::size_t index = 0;
                 index < candidate.scheduledDimensions.size();
                 ++index) {
                if (index)
                    out << ", ";
                out << variableName(
                    candidate.scheduledDimensions[index]);
            }
            out << "])";
        }
        out << " {\n";
        for (const StatementSchedule &statement :
             candidate.statements) {
            out << "    S" << statement.statement << " -> ";
            printSchedule(out, statement.components);
            out << "\n";
        }
        out << "    tree {\n";
        for (const ScheduleTreeNode &node :
             candidate.tree.nodes()) {
            out << "      n" << node.id << " "
                << treeNodeKindName(node.kind);
            if (node.parent)
                out << " parent=n" << *node.parent;
            if (node.kind == ScheduleTreeNodeKind::Band) {
                out << " dims=[";
                for (std::size_t index = 0;
                     index < node.band.dimensions.size();
                     ++index) {
                    if (index)
                        out << ", ";
                    out << variableName(
                        node.band.dimensions[index]);
                }
                out << "]";
            }
            if (node.kind == ScheduleTreeNodeKind::Filter) {
                out << " statements=[";
                for (std::size_t index = 0;
                     index < node.statements.size(); ++index) {
                    if (index)
                        out << ", ";
                    out << "S" << node.statements[index];
                }
                out << "]";
            }
            out << "\n";
        }
        out << "    }\n";
        out << "  }\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
