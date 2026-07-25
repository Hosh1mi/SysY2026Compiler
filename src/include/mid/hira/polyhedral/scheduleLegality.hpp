#pragma once

#include "dependenceFeasibility.hpp"
#include "scheduleAnalysis.hpp"

#include <string>
#include <vector>

namespace hira::polyhedral {

enum class ScheduledDependenceStatus {
    Preserved,
    Violated,
    Unknown,
    Irrelevant,
};

struct ScheduledDependence {
    DependenceId dependence = 0;
    ScheduledDependenceStatus status =
        ScheduledDependenceStatus::Unknown;
};

enum class ScheduleLegalityKind {
    Legal,
    Illegal,
    Unknown,
};

struct ScheduleLegality {
    ScheduleCandidateId schedule = 0;
    ScheduleLegalityKind kind = ScheduleLegalityKind::Unknown;
    std::vector<ScheduledDependence> dependences;
};

class ScheduleLegalityResult {
public:
    const std::vector<ScheduleLegality> &schedules() const {
        return schedules_;
    }

private:
    friend ScheduleLegalityResult analyzeScheduleLegality(
        const PolyhedralModel &model,
        const DependenceSet &dependences,
        const DependenceFeasibilityResult &feasibility,
        const ScheduleCandidateSet &schedules);

    std::vector<ScheduleLegality> schedules_;
};

ScheduleLegalityResult analyzeScheduleLegality(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules);
bool verifyScheduleLegality(
    const DependenceSet &dependences,
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &result,
    std::string &detail);
std::string printScheduleLegality(
    const ScheduleLegalityResult &result);

} // namespace hira::polyhedral
