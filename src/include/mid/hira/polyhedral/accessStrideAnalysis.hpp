#pragma once

#include "polyhedralModel.hpp"

#include <cstdint>
#include <optional>

namespace hira::polyhedral {

std::optional<std::int64_t> analyzeLinearAccessStride(
    const PolyhedralModel &model,
    const AccessRelation &access,
    AffineVariable dimension);

} // namespace hira::polyhedral
