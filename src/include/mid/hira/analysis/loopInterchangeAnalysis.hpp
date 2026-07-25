#pragma once

#include <cstddef>
#include <optional>

namespace hira {

class HiraLoop;
class HiraSequence;

namespace polyhedral {

class PolyhedralModel;

struct AdjacentLoopInterchangePlan {
    HiraSequence *innerSequence = nullptr;
    std::size_t innerPosition = 0;
};

std::optional<AdjacentLoopInterchangePlan>
analyzeAdjacentLoopInterchange(
    const PolyhedralModel &model, HiraLoop &outer,
    HiraLoop &inner);

} // namespace polyhedral
} // namespace hira
