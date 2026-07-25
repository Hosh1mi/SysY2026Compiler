#pragma once

#include "../polyhedral/statementPartitionAnalysis.hpp"

#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

enum class LoopDistributionError {
    None,
    Indivisible,
    UnsupportedDomain,
    LoopCarriedState,
    InvalidLoopBody,
    CrossPartitionScalar,
    UnsupportedNode,
};

struct LoopDistributionResult {
    bool changed = false;
    LoopDistributionError error =
        LoopDistributionError::None;
    std::string detail;

    bool succeeded() const {
        return error == LoopDistributionError::None;
    }
};

LoopDistributionResult distributeStatements(
    HiraRegion &region, const PolyhedralModel &model,
    const StatementPartitionResult &partitions);
const char *loopDistributionErrorName(
    LoopDistributionError error);

} // namespace polyhedral
} // namespace hira
