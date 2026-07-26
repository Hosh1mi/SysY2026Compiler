#pragma once

#include <cstddef>
#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

struct ScheduleFusionResult {
    bool changed = false;
    std::size_t fusedBands = 0;
    std::string detail;
};

ScheduleFusionResult fuseProvablyDisjointAdjacentBands(
    HiraRegion &region, const PolyhedralModel &model);

} // namespace polyhedral
} // namespace hira
