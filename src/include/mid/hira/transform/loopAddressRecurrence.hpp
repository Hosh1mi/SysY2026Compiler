#pragma once

#include "../polyhedral/affineExpr.hpp"

#include <cstddef>
#include <string>

namespace hira {

class HiraLoop;
class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

struct LoopAddressRecurrenceResult {
    bool changed = false;
    std::size_t recurrences = 0;
    std::string detail;
};

LoopAddressRecurrenceResult introduceLoopAddressRecurrences(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension);

LoopAddressRecurrenceResult introduceLoopAddressRecurrencesOnLoop(
    HiraRegion &region, HiraLoop &loop,
    const PolyhedralModel &model, AffineVariable dimension);

} // namespace polyhedral
} // namespace hira
