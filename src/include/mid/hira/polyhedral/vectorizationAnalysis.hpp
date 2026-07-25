#pragma once

#include "../target/a53TargetModel.hpp"
#include "dependenceFeasibility.hpp"
#include "scheduleAnalysis.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hira::polyhedral {

enum class VectorizationKind {
    Vectorizable,
    Scalar,
};

enum class VectorizationReason {
    None,
    MissingInnerDimension,
    NoVaryingAccess,
    NonContiguousAccess,
    LoopCarriedDependence,
    OpaqueControl,
    UnsupportedElementType,
    UnsupportedOperation,
};

struct ScheduleVectorization {
    ScheduleCandidateId schedule = 0;
    VectorizationKind kind = VectorizationKind::Scalar;
    VectorizationReason reason =
        VectorizationReason::MissingInnerDimension;
    std::optional<AffineVariable> dimension;
    std::uint32_t lanes = 1;
    std::vector<DependenceId> blockers;
};

class VectorizationAnalysisResult {
public:
    const std::vector<ScheduleVectorization> &schedules() const {
        return schedules_;
    }

private:
    friend VectorizationAnalysisResult analyzeVectorization(
        const PolyhedralModel &model,
        const DependenceSet &dependences,
        const DependenceFeasibilityResult &feasibility,
        const ScheduleCandidateSet &schedules,
        const target::A53TargetModel &target);

    std::vector<ScheduleVectorization> schedules_;
};

VectorizationAnalysisResult analyzeVectorization(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target =
        target::cortexA53());
bool verifyVectorizationAnalysis(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target,
    const VectorizationAnalysisResult &result,
    std::string &detail);
std::string printVectorizationAnalysis(
    const VectorizationAnalysisResult &result);

} // namespace hira::polyhedral
