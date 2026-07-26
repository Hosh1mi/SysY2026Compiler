#pragma once

#include "../polyhedral/statementPartitionAnalysis.hpp"

#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

enum class StatementPartitionRealizationError {
    None,
    Indivisible,
    UnsupportedDomain,
    LoopCarriedState,
    InvalidLoopBody,
    CrossPartitionScalar,
    UnsupportedNode,
};

struct StatementPartitionRealizationResult {
    bool changed = false;
    StatementPartitionRealizationError error =
        StatementPartitionRealizationError::None;
    std::string detail;

    bool succeeded() const {
        return error ==
               StatementPartitionRealizationError::None;
    }
};

StatementPartitionRealizationResult realizeStatementPartitions(
    HiraRegion &region, const PolyhedralModel &model,
    const StatementPartitionResult &partitions);
const char *statementPartitionRealizationErrorName(
    StatementPartitionRealizationError error);

} // namespace polyhedral
} // namespace hira
