#pragma once

#include "dependenceFeasibility.hpp"
#include "reductionAnalysis.hpp"
#include "scheduleAnalysis.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hira::polyhedral {

struct ScheduleParallelism {
    ScheduleCandidateId schedule = 0;
    std::optional<AffineVariable> outerDimension;
    bool outerParallel = false;
    std::vector<DependenceId> blockers;
};

class ScheduleParallelismResult {
public:
    const std::vector<ScheduleParallelism> &schedules() const {
        return schedules_;
    }

private:
    friend ScheduleParallelismResult analyzeScheduleParallelism(
        const PolyhedralModel &model,
        const DependenceSet &dependences,
        const DependenceFeasibilityResult &feasibility,
        const ReductionAnalysisResult &reductions,
        const ScheduleCandidateSet &schedules);

    std::vector<ScheduleParallelism> schedules_;
};

ScheduleParallelismResult analyzeScheduleParallelism(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ReductionAnalysisResult &reductions,
    const ScheduleCandidateSet &schedules);
bool verifyScheduleParallelism(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ReductionAnalysisResult &reductions,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &result,
    std::string &detail);
std::string printScheduleParallelism(
    const ScheduleParallelismResult &result);

} // namespace hira::polyhedral
