#include "../../../include/backend/arm64/rewrite/constant_division.hpp"

#include <cassert>
#include <climits>

namespace backend::aarch64::division {

SignedDivisorInfo analyzeSignedDivisor(std::int32_t divisor) {
    SignedDivisorInfo info;
    info.value = divisor;
    if (divisor == 0 || divisor == INT32_MIN)
        return info;

    info.reducible = true;
    info.magnitude =
        divisor < 0 ? 0U - static_cast<std::uint32_t>(divisor)
                    : static_cast<std::uint32_t>(divisor);
    info.powerOfTwo =
        (info.magnitude & (info.magnitude - 1)) == 0;
    if (info.powerOfTwo)
        info.shift = __builtin_ctz(info.magnitude);
    return info;
}

MagicNumber computeSignedMagic(std::int32_t divisor) {
    SignedDivisorInfo info = analyzeSignedDivisor(divisor);
    assert(info.usesMagic());

    int precision = 31;
    std::uint32_t absoluteDivisor = info.magnitude;
    std::uint32_t signAdjusted =
        0x80000000U +
        (static_cast<std::uint32_t>(divisor) >> 31);
    std::uint32_t cutoff =
        signAdjusted - 1 -
        signAdjusted % absoluteDivisor;
    std::uint32_t quotient1 = 0x80000000U / cutoff;
    std::uint32_t remainder1 =
        0x80000000U - quotient1 * cutoff;
    std::uint32_t quotient2 =
        0x80000000U / absoluteDivisor;
    std::uint32_t remainder2 =
        0x80000000U - quotient2 * absoluteDivisor;
    std::uint32_t delta = 0;

    do {
        ++precision;
        quotient1 *= 2;
        remainder1 *= 2;
        if (remainder1 >= cutoff) {
            ++quotient1;
            remainder1 -= cutoff;
        }
        quotient2 *= 2;
        remainder2 *= 2;
        if (remainder2 >= absoluteDivisor) {
            ++quotient2;
            remainder2 -= absoluteDivisor;
        }
        delta = absoluteDivisor - remainder2;
    } while (quotient1 < delta ||
             (quotient1 == delta && remainder1 == 0));

    std::int32_t multiplier =
        static_cast<std::int32_t>(quotient2 + 1);
    if (divisor < 0)
        multiplier = -multiplier;

    MagicNumber magic;
    magic.multiplier = multiplier;
    magic.shift = precision - 32;
    if (divisor > 0 && multiplier < 0)
        magic.strategy = MagicStrategy::MultiplyAddShift;
    else if (divisor < 0 && multiplier > 0)
        magic.strategy = MagicStrategy::MultiplySubShift;
    return magic;
}

BarrettModulus64 computeBarrettModulus64(std::uint32_t modulus) {
    assert(modulus > 1);
    BarrettModulus64 info;
    info.modulus = modulus;
    info.mu = static_cast<std::uint64_t>(
        (static_cast<__uint128_t>(1) << 64) / modulus);
    return info;
}

} // namespace backend::aarch64::division
