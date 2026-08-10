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

// Barrett factor μ = floor(2^64 / m) for reducing a 64-bit unsigned
// numerator modulo a positive 32-bit modulus.  One conditional subtract
// after `r = n - umulh(n, μ) * m` is sufficient.
struct BarrettModulus64 {
    std::uint64_t mu = 0;
    std::uint32_t modulus = 0;
};

SignedDivisorInfo analyzeSignedDivisor(std::int32_t divisor);
MagicNumber computeSignedMagic(std::int32_t divisor);
BarrettModulus64 computeBarrettModulus64(std::uint32_t modulus);

} // namespace backend::aarch64::division
