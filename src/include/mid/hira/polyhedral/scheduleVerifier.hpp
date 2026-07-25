#pragma once

#include "scheduleAnalysis.hpp"

#include <string>

namespace hira::polyhedral {

bool verifyScheduleCandidates(const PolyhedralModel &model,
                              const ScheduleCandidateSet &schedules,
                              std::string &detail);

} // namespace hira::polyhedral
