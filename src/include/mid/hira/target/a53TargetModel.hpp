#pragma once

#include <cstddef>
#include <cstdint>

namespace hira::target {

// The Hira pipeline targets the fixed Cortex-A53 evaluation platform.  Keep
// the physical machine description separate from scheduling policy so every
// transform uses the same cache, SIMD and worker assumptions.
struct A53TargetModel {
    std::size_t l1DataBytes = 32 * 1024;
    std::uint32_t l1DataAssociativity = 4;
    std::size_t l1UsableBytes = 24 * 1024;
    std::size_t l2Bytes = 1024 * 1024;
    std::size_t cacheLineBytes = 64;
    std::uint32_t evaluationWorkers = 2;
    std::uint32_t neonBits = 128;
    std::uint32_t float32Lanes = 4;
    std::uint32_t float64Lanes = 1;

    // These limits bound compile-time schedule exploration rather than
    // changing program semantics.
    std::size_t maxScheduleStates = 128;
    std::size_t maxTileChoicesPerBand = 6;
    std::uint32_t minimumParallelTilesPerWorker = 4;
    std::uint32_t minimumParallelOverheadRatio = 8;
};

inline constexpr A53TargetModel cortexA53() {
    return {};
}

} // namespace hira::target
