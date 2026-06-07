#pragma once

#include <string>
#include <vector>

class MachineScheduler {
public:
    std::string scheduleFunctionText(const std::string &asmText) const;

private:
    std::string scheduleSegment(const std::vector<std::string> &segment,
                                int firstOriginalIndex) const;
};
