#pragma once

#include "../polyhedral/polyhedralModel.hpp"

namespace hira {

class HiraLoop;
class HiraRegion;

namespace polyhedral {

bool leadingPayloadBlocksInterchange(
    const PolyhedralModel &model, const HiraLoop &outer,
    const HiraLoop &inner);

bool distributeLeadingPayload(
    HiraRegion &region, const PolyhedralModel &model,
    HiraLoop &outer, HiraLoop &inner);

bool distributeTrailingPayload(
    HiraRegion &region, const PolyhedralModel &model,
    HiraLoop &outer, HiraLoop &inner);

bool normalizeBandForPermutation(
    HiraRegion &region, const PolyhedralModel &model,
    const std::vector<AffineVariable> &bandDimensions);

} // namespace polyhedral
} // namespace hira
