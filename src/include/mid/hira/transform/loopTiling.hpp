#pragma once

#include "../polyhedral/polyhedralModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira {

class HiraRegion;

namespace polyhedral {

enum class LoopTilingError {
    None,
    InvalidTileSize,
    MissingLoopDomain,
    NonUnitPointLoop,
    LoopCarriedState,
    InvalidNest,
};

struct LoopTilingResult {
    bool changed = false;
    LoopTilingError error = LoopTilingError::None;
    std::string detail;

    bool succeeded() const {
        return error == LoopTilingError::None;
    }
};

LoopTilingResult stripMineLoop(
    HiraRegion &region, const PolyhedralModel &model,
    AffineVariable dimension, std::uint32_t tileSize);
LoopTilingResult tileLoopBand(
    HiraRegion &region, const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions,
    const std::vector<std::uint32_t> &tileSizes);
const char *loopTilingErrorName(LoopTilingError error);

} // namespace polyhedral
} // namespace hira
