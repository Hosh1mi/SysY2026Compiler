#pragma once

#include "statementDependenceGraph.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira::polyhedral {

using StatementPartitionId = std::uint32_t;

struct StatementPartition {
    StatementPartitionId id = 0;
    std::vector<StatementId> statements;
    std::vector<AffineVariable> dimensions;
    bool fusibleWithNext = false;
};

enum class StatementPartitionKind {
    Distributable,
    Indivisible,
};

enum class StatementPartitionReason {
    None,
    SingleGroup,
    IncompatibleDomains,
};

class StatementPartitionResult {
public:
    StatementPartitionKind kind() const { return kind_; }
    StatementPartitionReason reason() const { return reason_; }
    const std::vector<StatementPartition> &partitions() const {
        return partitions_;
    }
    const std::vector<StatementPartitionId> &
    partitionByStatement() const {
        return partitionByStatement_;
    }

private:
    friend StatementPartitionResult analyzeStatementPartitions(
        const PolyhedralModel &model,
        const DependenceSet &dependences,
        const DependenceFeasibilityResult &feasibility,
        const StatementDependenceGraph &graph);

    StatementPartitionKind kind_ =
        StatementPartitionKind::Indivisible;
    StatementPartitionReason reason_ =
        StatementPartitionReason::SingleGroup;
    std::vector<StatementPartition> partitions_;
    std::vector<StatementPartitionId> partitionByStatement_;
};

StatementPartitionResult analyzeStatementPartitions(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph);
bool verifyStatementPartitions(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph,
    const StatementPartitionResult &result,
    std::string &detail);
std::string printStatementPartitions(
    const StatementPartitionResult &result);

} // namespace hira::polyhedral
