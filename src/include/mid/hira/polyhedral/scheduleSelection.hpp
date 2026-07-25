#pragma once

#include "scheduleApplicability.hpp"
#include "scheduleLegality.hpp"
#include "scheduleProfitability.hpp"

#include <string>
#include <vector>

namespace hira::polyhedral {

enum class ScheduleSelectionDecision {
    Selected,
    Baseline,
    RejectedLegality,
    RejectedApplicability,
    RejectedProfitability,
    LowerBenefit,
};

struct ScheduleSelectionEntry {
    ScheduleCandidateId schedule = 0;
    ScheduleSelectionDecision decision =
        ScheduleSelectionDecision::RejectedProfitability;
};

class ScheduleSelectionResult {
public:
    ScheduleCandidateId selected() const { return selected_; }
    const std::vector<ScheduleSelectionEntry> &entries() const {
        return entries_;
    }

private:
    friend ScheduleSelectionResult selectSchedule(
        const ScheduleCandidateSet &schedules,
        const ScheduleLegalityResult &legality,
        const ScheduleApplicabilityResult &applicability,
        const ScheduleProfitabilityResult &profitability);

    ScheduleCandidateId selected_ = 0;
    std::vector<ScheduleSelectionEntry> entries_;
};

ScheduleSelectionResult selectSchedule(
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleProfitabilityResult &profitability);
bool verifyScheduleSelection(
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleProfitabilityResult &profitability,
    const ScheduleSelectionResult &selection,
    std::string &detail);
std::string printScheduleSelection(
    const ScheduleSelectionResult &selection);

} // namespace hira::polyhedral
