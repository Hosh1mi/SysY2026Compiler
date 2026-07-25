#pragma once

#include "scheduleAnalysis.hpp"

#include <string>
#include <vector>

namespace hira::polyhedral {

enum class ScheduleApplicabilityKind {
    Realizable,
    Unsupported,
};

enum class ScheduleApplicabilityReason {
    None,
    UnsupportedCandidate,
    MissingLoopDomain,
    NonUnitStep,
    LoopCarriedState,
    ImperfectNest,
    NonCanonicalLatch,
    CoupledBounds,
};

struct ScheduleApplicability {
    ScheduleCandidateId schedule = 0;
    ScheduleApplicabilityKind kind =
        ScheduleApplicabilityKind::Unsupported;
    ScheduleApplicabilityReason reason =
        ScheduleApplicabilityReason::None;
};

class ScheduleApplicabilityResult {
public:
    const std::vector<ScheduleApplicability> &schedules() const {
        return schedules_;
    }

private:
    friend ScheduleApplicabilityResult
    analyzeScheduleApplicability(
        const PolyhedralModel &model,
        const ScheduleCandidateSet &schedules);

    std::vector<ScheduleApplicability> schedules_;
};

ScheduleApplicabilityResult analyzeScheduleApplicability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules);
bool verifyScheduleApplicability(
    const ScheduleCandidateSet &schedules,
    const ScheduleApplicabilityResult &result,
    std::string &detail);
std::string printScheduleApplicability(
    const ScheduleApplicabilityResult &result);

} // namespace hira::polyhedral
