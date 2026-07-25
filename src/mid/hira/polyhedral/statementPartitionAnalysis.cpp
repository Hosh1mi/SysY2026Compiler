#include "../../../include/mid/hira/polyhedral/statementPartitionAnalysis.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace hira::polyhedral {
namespace {

class DisjointSets {
public:
    explicit DisjointSets(std::size_t size)
        : parent_(size), rank_(size, 0) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    std::size_t find(std::size_t value) {
        if (parent_[value] != value)
            parent_[value] = find(parent_[value]);
        return parent_[value];
    }

    void unite(std::size_t left, std::size_t right) {
        left = find(left);
        right = find(right);
        if (left == right)
            return;
        if (rank_[left] < rank_[right])
            std::swap(left, right);
        parent_[right] = left;
        if (rank_[left] == rank_[right])
            ++rank_[left];
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::uint32_t> rank_;
};

bool isScalarDependence(DependenceKind kind) {
    return kind == DependenceKind::ScalarFlow ||
           kind == DependenceKind::RecurrenceCarried ||
           kind == DependenceKind::RecurrenceResult ||
           kind ==
               DependenceKind::RecurrenceInitialization;
}

bool mustRemainTogether(
    const DependenceRelation &relation,
    const DependenceFeasibility &feasibility) {
    if (feasibility.kind ==
        DependenceFeasibilityKind::ProvenEmpty)
        return false;
    // Scalar SSA values cannot cross a distributed loop without explicit
    // expansion.  Conservative memory relations also remain fused until a
    // more precise common analysis proves them independent.
    return isScalarDependence(relation.kind) ||
           relation.precision != DependencePrecision::Exact;
}

bool same(const StatementPartitionResult &left,
          const StatementPartitionResult &right) {
    if (left.kind() != right.kind() ||
        left.reason() != right.reason() ||
        left.partitionByStatement() !=
            right.partitionByStatement() ||
        left.partitions().size() !=
            right.partitions().size())
        return false;
    for (std::size_t index = 0;
         index < left.partitions().size(); ++index) {
        const auto &a = left.partitions()[index];
        const auto &b = right.partitions()[index];
        if (a.id != b.id ||
            a.statements != b.statements ||
            a.dimensions != b.dimensions ||
            a.fusibleWithNext != b.fusibleWithNext)
            return false;
    }
    return true;
}

const char *reasonName(StatementPartitionReason reason) {
    switch (reason) {
    case StatementPartitionReason::None:
        return "none";
    case StatementPartitionReason::SingleGroup:
        return "single-group";
    case StatementPartitionReason::IncompatibleDomains:
        return "incompatible-domains";
    }
    return "unknown";
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

StatementPartitionResult analyzeStatementPartitions(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph) {
    StatementPartitionResult result;
    DisjointSets sets(graph.components().size());

    for (std::size_t index = 0;
         index < dependences.relations().size(); ++index) {
        const DependenceRelation &relation =
            dependences.relations()[index];
        if (!relation.sourceStatement ||
            !relation.sinkStatement ||
            feasibility.relations()[index].kind ==
                DependenceFeasibilityKind::ProvenEmpty)
            continue;
        if (mustRemainTogether(
                relation, feasibility.relations()[index]) ||
            *relation.sourceStatement >
                *relation.sinkStatement)
            sets.unite(
                graph.componentByStatement()[
                    *relation.sourceStatement],
                graph.componentByStatement()[
                    *relation.sinkStatement]);
    }

    std::vector<std::vector<StatementId>> groups;
    std::vector<std::size_t> rootToGroup(
        graph.components().size(),
        graph.components().size());
    for (const StatementComponent &component :
         graph.components()) {
        std::size_t root = sets.find(component.id);
        if (rootToGroup[root] == graph.components().size()) {
            rootToGroup[root] = groups.size();
            groups.push_back({});
        }
        auto &group = groups[rootToGroup[root]];
        group.insert(group.end(),
                     component.statements.begin(),
                     component.statements.end());
    }
    for (auto &group : groups)
        std::sort(group.begin(), group.end());
    std::sort(groups.begin(), groups.end(),
              [](const auto &left, const auto &right) {
                  return left.front() < right.front();
              });

    result.partitionByStatement_.assign(
        model.statements().size(), 0);
    bool compatibleDomains = true;
    for (std::size_t index = 0;
         index < groups.size(); ++index) {
        StatementPartition partition;
        partition.id =
            static_cast<StatementPartitionId>(index);
        partition.statements = std::move(groups[index]);
        partition.dimensions =
            model.statements()[
                     partition.statements.front()]
                .dimensions;
        for (StatementId statement :
             partition.statements) {
            result.partitionByStatement_[statement] =
                partition.id;
            compatibleDomains &=
                model.statements()[statement].dimensions ==
                partition.dimensions;
        }
        result.partitions_.push_back(
            std::move(partition));
    }
    for (std::size_t index = 0;
         index + 1 < result.partitions_.size(); ++index)
        result.partitions_[index].fusibleWithNext =
            result.partitions_[index].dimensions ==
            result.partitions_[index + 1].dimensions;
    if (!result.partitions_.empty())
        for (const StatementPartition &partition :
             result.partitions_)
            compatibleDomains &=
                partition.dimensions ==
                result.partitions_.front().dimensions;

    if (!compatibleDomains) {
        result.kind_ = StatementPartitionKind::Indivisible;
        result.reason_ =
            StatementPartitionReason::IncompatibleDomains;
    } else if (result.partitions_.size() < 2) {
        result.kind_ = StatementPartitionKind::Indivisible;
        result.reason_ =
            StatementPartitionReason::SingleGroup;
    } else {
        result.kind_ =
            StatementPartitionKind::Distributable;
        result.reason_ = StatementPartitionReason::None;
    }
    return result;
}

bool verifyStatementPartitions(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph,
    const StatementPartitionResult &result,
    std::string &detail) {
    StatementPartitionResult expected =
        analyzeStatementPartitions(
            model, dependences, feasibility, graph);
    if (!same(result, expected)) {
        detail = "invalid-statement-partitions";
        return false;
    }
    return true;
}

std::string printStatementPartitions(
    const StatementPartitionResult &result) {
    std::ostringstream out;
    out << "polyhedral.statement_partitions = "
        << (result.kind() ==
                    StatementPartitionKind::Distributable
                ? "distributable"
                : "indivisible");
    if (result.reason() !=
        StatementPartitionReason::None)
        out << " reason=" << reasonName(result.reason());
    out << " {\n";
    for (const StatementPartition &partition :
         result.partitions()) {
        out << "  P" << partition.id << " statements=[";
        for (std::size_t index = 0;
             index < partition.statements.size(); ++index) {
            if (index)
                out << ", ";
            out << "S" << partition.statements[index];
        }
        out << "] dims=[";
        for (std::size_t index = 0;
             index < partition.dimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << dimensionName(
                partition.dimensions[index]);
        }
        out << "]";
        if (partition.fusibleWithNext)
            out << " fusible-next";
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
