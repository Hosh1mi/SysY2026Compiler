#pragma once

#include <string>

namespace hira {

class HiraRegion;

std::string printHiraRegion(const HiraRegion &region,
                            const std::string &functionName);

} // namespace hira
