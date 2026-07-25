#pragma once

#include "polyhedralModel.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hira::polyhedral {

using DependenceId = std::uint32_t;
using AccessId = std::uint32_t;

class DependenceBuilder;
struct DependenceBuildResult;

enum class DependenceKind {
    ScalarFlow,
    RecurrenceCarried,
    RecurrenceResult,
    RecurrenceInitialization,
    MemoryRAW,
    MemoryWAR,
    MemoryWAW,
};

enum class DependencePrecision {
    Exact,
    ConservativeAlias,
    ConservativeShape,
};

enum class DependenceOrdering {
    IdentityBefore,
    NextIteration,
    LastIteration,
    LoopEntry,
};

struct AffineEquality {
    AffineExpr source;
    AffineExpr sink;
};

struct DimensionDistance {
    AffineVariable source;
    AffineVariable sink;
    std::int64_t distance = 0;
};

struct DependenceRelation {
    DependenceId id = 0;
    DependenceKind kind = DependenceKind::ScalarFlow;
    DependencePrecision precision = DependencePrecision::Exact;
    DependenceOrdering ordering =
        DependenceOrdering::IdentityBefore;
    std::optional<StatementId> sourceStatement;
    std::optional<StatementId> sinkStatement;
    std::optional<AccessId> sourceAccess;
    std::optional<AccessId> sinkAccess;
    std::optional<ScalarRecurrenceId> sourceRecurrence;
    std::optional<ScalarRecurrenceId> sinkRecurrence;
    std::vector<DimensionDistance> dimensionDistances;
    std::vector<AffineEquality> accessEqualities;
};

class DependenceSet {
public:
    const std::vector<DependenceRelation> &relations() const {
        return relations_;
    }

private:
    friend class DependenceBuilder;

    std::vector<DependenceRelation> relations_;
};

enum class DependenceBuildError {
    None,
    InvalidModelReference,
};

struct DependenceBuildResult {
    std::unique_ptr<DependenceSet> dependences;
    DependenceBuildError error = DependenceBuildError::None;
    std::string detail;

    bool succeeded() const {
        return dependences && error == DependenceBuildError::None;
    }
};

const char *dependenceBuildErrorName(DependenceBuildError error);
DependenceBuildResult
buildDependenceRelations(const PolyhedralModel &model);
std::string printDependenceRelations(const DependenceSet &dependences);

} // namespace hira::polyhedral
