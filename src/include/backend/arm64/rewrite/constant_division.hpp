#pragma once

#include <cstdint>

namespace backend::aarch64::division {

enum class MagicStrategy : std::uint8_t {
    MultiplyShift,
    MultiplyAddShift,
    MultiplySubShift,
};

struct MagicNumber {
    std::int32_t multiplier = 0;
    int shift = 0;
    MagicStrategy strategy = MagicStrategy::MultiplyShift;
};

struct SignedDivisorInfo {
    std::int32_t value = 0;
    std::uint32_t magnitude = 0;
    bool reducible = false;
    bool powerOfTwo = false;
    int shift = 0;

    bool usesMagic() const {
        return reducible && magnitude > 1 && !powerOfTwo;
    }
};

SignedDivisorInfo analyzeSignedDivisor(std::int32_t divisor);
MagicNumber computeSignedMagic(std::int32_t divisor);

} // namespace backend::aarch64::division
