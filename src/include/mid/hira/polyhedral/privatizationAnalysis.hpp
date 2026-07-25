#pragma once

#include "reductionAnalysis.hpp"
#include "scheduleParallelism.hpp"

#include <string>
#include <vector>

namespace hira::polyhedral {

enum class PrivatizationKind {
    TaskPrivateScalar,
    WorkerReduction,
    Sequential,
};

struct RecurrencePrivatization {
    ScalarRecurrenceId recurrence = 0;
    PrivatizationKind kind =
        PrivatizationKind::Sequential;
};

struct SchedulePrivatization {
    ScheduleCandidateId schedule = 0;
    std::vector<RecurrencePrivatization> recurrences;
};

class PrivatizationAnalysisResult {
public:
    const std::vector<SchedulePrivatization> &schedules() const {
        return schedules_;
    }

private:
    friend PrivatizationAnalysisResult analyzePrivatization(
        const PolyhedralModel &model,
        const ScheduleCandidateSet &schedules,
        const ScheduleParallelismResult &parallelism,
        const ReductionAnalysisResult &reductions);

    std::vector<SchedulePrivatization> schedules_;
};

PrivatizationAnalysisResult analyzePrivatization(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &parallelism,
    const ReductionAnalysisResult &reductions);
bool verifyPrivatizationAnalysis(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &parallelism,
    const ReductionAnalysisResult &reductions,
    const PrivatizationAnalysisResult &result,
    std::string &detail);
std::string printPrivatizationAnalysis(
    const PrivatizationAnalysisResult &result);

} // namespace hira::polyhedral
