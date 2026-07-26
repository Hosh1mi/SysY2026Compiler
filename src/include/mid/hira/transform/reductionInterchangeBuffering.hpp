#pragma once

#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

enum class ReductionInterchangeBufferingError {
    None,
    NoCandidate,
    AmbiguousCandidate,
    UnsupportedStructure,
    UnsupportedReduction,
    UnsafeMemoryEffects,
    UnprovenStoreback,
    UnprofitableInterchange,
    UnsupportedScratchType,
};

struct ReductionInterchangeBufferingResult {
    bool changed = false;
    ReductionInterchangeBufferingError error =
        ReductionInterchangeBufferingError::None;
    std::string detail;

    bool succeeded() const {
        return error == ReductionInterchangeBufferingError::None;
    }
};

ReductionInterchangeBufferingResult
bufferReductionInterchange(HiraRegion &region,
                           const PolyhedralModel &model);

const char *reductionInterchangeBufferingErrorName(
    ReductionInterchangeBufferingError error);

} // namespace polyhedral
} // namespace hira
