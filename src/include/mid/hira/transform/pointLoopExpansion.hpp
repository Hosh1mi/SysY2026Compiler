#pragma once

#include "../polyhedral/affineExpr.hpp"

#include <cstdint>
#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

enum class PointLoopExpansionError {
    None,
    MissingLoopDomain,
    UnsupportedLoop,
    UnsupportedBody,
    UnsupportedFactor,
};

struct PointLoopExpansionResult {
    bool changed = false;
    PointLoopExpansionError error =
        PointLoopExpansionError::None;
    std::string detail;

    bool succeeded() const {
        return error == PointLoopExpansionError::None;
    }
};

PointLoopExpansionResult expandPointLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t factor);
const char *pointLoopExpansionErrorName(
    PointLoopExpansionError error);

} // namespace polyhedral
} // namespace hira
