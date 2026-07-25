#pragma once

#include "../target/a53TargetModel.hpp"
#include "scheduleAnalysis.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira::polyhedral {

enum class CacheFootprintKind {
    Known,
    Unknown,
};

struct CacheFootprint {
    ScheduleCandidateId schedule = 0;
    CacheFootprintKind kind = CacheFootprintKind::Unknown;
    std::uint64_t l1FootprintBytes = 0;
    std::uint64_t tileVolume = 0;
    std::vector<AffineVariable> dimensions;
    std::vector<std::uint32_t> tileSizes;
};

class CacheFootprintResult {
public:
    const std::vector<CacheFootprint> &schedules() const {
        return schedules_;
    }

private:
    friend CacheFootprintResult analyzeCacheFootprints(
        const PolyhedralModel &model,
        const ScheduleCandidateSet &schedules,
        const target::A53TargetModel &target);

    std::vector<CacheFootprint> schedules_;
};

CacheFootprintResult analyzeCacheFootprints(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target =
        target::cortexA53());
bool verifyCacheFootprints(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target,
    const CacheFootprintResult &result,
    std::string &detail);
std::string printCacheFootprints(
    const CacheFootprintResult &result);

} // namespace hira::polyhedral
