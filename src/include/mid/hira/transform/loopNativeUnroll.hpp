#pragma once

#include <cstddef>
#include <string>

namespace hira {

class HiraRegion;

struct LoopNativeUnrollResult {
    bool changed = false;
    std::size_t loops = 0;
    std::string detail;
};

LoopNativeUnrollResult unrollCountedLoops(HiraRegion &region);

} // namespace hira
