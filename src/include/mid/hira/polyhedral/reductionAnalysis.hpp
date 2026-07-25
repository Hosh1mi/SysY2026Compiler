#pragma once

#include "polyhedralModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hira::polyhedral {

enum class ReductionOperator {
    Add,
    Multiply,
    BitAnd,
    BitOr,
    BitXor,
};

enum class ReductionParallelSemantics {
    Exact,
    SequentialOnly,
};

struct ScalarReduction {
    ScalarRecurrenceId recurrence = 0;
    ReductionOperator operation = ReductionOperator::Add;
    ReductionParallelSemantics parallelSemantics =
        ReductionParallelSemantics::SequentialOnly;
    const HiraValue *input = nullptr;
    std::optional<std::int64_t> integerIdentity;
};

class ReductionAnalysisResult {
public:
    const std::vector<ScalarReduction> &scalarReductions() const {
        return scalarReductions_;
    }
    const std::vector<ScalarRecurrenceId> &
    nonReductions() const {
        return nonReductions_;
    }

private:
    friend ReductionAnalysisResult analyzeReductions(
        const PolyhedralModel &model);

    std::vector<ScalarReduction> scalarReductions_;
    std::vector<ScalarRecurrenceId> nonReductions_;
};

ReductionAnalysisResult analyzeReductions(
    const PolyhedralModel &model);
bool verifyReductionAnalysis(
    const PolyhedralModel &model,
    const ReductionAnalysisResult &result,
    std::string &detail);
std::string printReductionAnalysis(
    const ReductionAnalysisResult &result);

} // namespace hira::polyhedral
