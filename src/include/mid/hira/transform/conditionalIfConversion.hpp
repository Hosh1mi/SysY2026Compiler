#pragma once

namespace hira {

class HiraRegion;

// Converts side-effect-free diamonds with explicit SSA results into eager
// computations followed by selects.  Potentially trapping operations and
// memory operations are deliberately excluded.
bool convertPureConditionals(HiraRegion &region);

} // namespace hira
