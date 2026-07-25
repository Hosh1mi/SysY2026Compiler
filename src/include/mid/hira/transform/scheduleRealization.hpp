#pragma once

#include "../polyhedral/scheduleAnalysis.hpp"
#include "../polyhedral/scheduleSelection.hpp"

#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

enum class ScheduleRealizationError {
    None,
    InvalidSelection,
    UnsupportedCandidate,
    MissingLoopDomain,
    InvalidNest,
};

struct ScheduleRealizationResult {
    bool changed = false;
    ScheduleCandidateId schedule = 0;
    ScheduleRealizationError error =
        ScheduleRealizationError::None;
    std::string detail;

    bool succeeded() const {
        return error == ScheduleRealizationError::None;
    }
};

ScheduleRealizationResult realizeSelectedSchedule(
    HiraRegion &region, const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleSelectionResult &selection);

bool verifyScheduleRealization(
    const PolyhedralModel &before,
    const ScheduleCandidate &candidate,
    const PolyhedralModel &after, std::string &detail);

const char *scheduleRealizationErrorName(
    ScheduleRealizationError error);

} // namespace polyhedral
} // namespace hira
