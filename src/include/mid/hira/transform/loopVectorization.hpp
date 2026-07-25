#pragma once

#include "../polyhedral/affineExpr.hpp"

#include <cstdint>
#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

enum class LoopVectorizationError {
    None,
    MissingLoopDomain,
    UnsupportedLoop,
    UnsupportedBody,
    UnsupportedAccess,
    UnsupportedType,
};

struct LoopVectorizationResult {
    bool changed = false;
    LoopVectorizationError error =
        LoopVectorizationError::None;
    std::string detail;

    bool succeeded() const {
        return error == LoopVectorizationError::None;
    }
};

LoopVectorizationResult vectorizeLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t lanes);
const char *loopVectorizationErrorName(
    LoopVectorizationError error);

} // namespace polyhedral
} // namespace hira
