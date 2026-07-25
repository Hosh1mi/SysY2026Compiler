#pragma once

#include "polyhedralModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira::polyhedral {

using ScheduleCandidateId = std::uint32_t;

enum class ScheduleCandidateKind {
    Identity,
    Interchange,
};

struct StatementSchedule {
    StatementId statement = 0;
    std::vector<ScheduleComponent> components;
};

struct ScheduleCandidate {
    ScheduleCandidateId id = 0;
    ScheduleCandidateKind kind = ScheduleCandidateKind::Identity;
    AffineVariable outerDimension;
    AffineVariable innerDimension;
    std::vector<StatementSchedule> statements;
};

class ScheduleCandidateSet {
public:
    const std::vector<ScheduleCandidate> &candidates() const {
        return candidates_;
    }

private:
    friend ScheduleCandidateSet
    buildScheduleCandidates(const PolyhedralModel &model);

    std::vector<ScheduleCandidate> candidates_;
};

ScheduleCandidateSet
buildScheduleCandidates(const PolyhedralModel &model);
std::string printScheduleCandidates(
    const ScheduleCandidateSet &schedules);

} // namespace hira::polyhedral
