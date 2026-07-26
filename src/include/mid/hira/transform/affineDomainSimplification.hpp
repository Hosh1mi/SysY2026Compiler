#pragma once

namespace hira {

class HiraRegion;

// Restricts a loop to the active prefix or suffix of an affine guard when
// the complementary arm is empty.  This exposes a branch-free interior
// domain without duplicating side effects.
bool extractAffineInteriorDomains(HiraRegion &region);

} // namespace hira
