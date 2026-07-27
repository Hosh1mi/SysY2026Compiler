#pragma once

#include "scheduleAnalysis.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira::polyhedral {

enum class ScheduleProfitabilityKind {
    Baseline,
    ProvenBeneficial,
    Neutral,
    Regressing,
    Unknown,
};

enum class ScheduleProfitabilityReason {
    None,
    NoMemoryAccess,
    UnknownStride,
    NoStrictImprovement,
    AccessStrideRegression,
    DestroysUnitStrideVectorization,
};

struct AccessStrideChange {
    std::uint32_t access = 0;
    std::int64_t baselineBytes = 0;
    std::int64_t candidateBytes = 0;
};

struct ScheduleProfitability {
    ScheduleCandidateId schedule = 0;
    ScheduleProfitabilityKind kind =
        ScheduleProfitabilityKind::Unknown;
    ScheduleProfitabilityReason reason =
        ScheduleProfitabilityReason::None;
    std::int64_t totalStrideReduction = 0;
    std::vector<AccessStrideChange> accesses;
};

class ScheduleProfitabilityResult {
public:
    const std::vector<ScheduleProfitability> &schedules() const {
        return schedules_;
    }

private:
    friend ScheduleProfitabilityResult
    analyzeScheduleProfitability(
        const PolyhedralModel &model,
        const ScheduleCandidateSet &schedules);

    std::vector<ScheduleProfitability> schedules_;
};

ScheduleProfitabilityResult analyzeScheduleProfitability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules);
bool verifyScheduleProfitability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleProfitabilityResult &result,
    std::string &detail);
std::string printScheduleProfitability(
    const ScheduleProfitabilityResult &result);

} // namespace hira::polyhedral
