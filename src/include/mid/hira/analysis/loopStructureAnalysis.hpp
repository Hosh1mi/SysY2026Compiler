#pragma once

#include <optional>

namespace hira {

class HiraComputeOp;
class HiraLoop;
class HiraYield;

struct CanonicalLoopControl {
    const HiraComputeOp *inductionUpdate = nullptr;
    const HiraYield *yield = nullptr;
};

std::optional<CanonicalLoopControl>
analyzeCanonicalLoopControl(const HiraLoop &loop);

bool isPerfectLoopNest(
    const HiraLoop &outer, const HiraLoop &inner);

} // namespace hira
