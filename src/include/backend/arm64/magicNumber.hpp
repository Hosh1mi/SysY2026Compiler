/*
* Borrowed from Hacker's Delight.
* Assuming:
*   w0 = dividend (n)
*   w1 = magic multiplier (M) loaded via mov/movk
* 
* Case 1: MULTIPLY_SHIFT 
* (Example: n / 3 -> M = 0x55555556, shift = 0)
* ---------------------------------------------------------------------------------
*   smull   x2, w0, w1          // Multiply n by M to get 64-bit result
*   asr     x2, x2, #32         // Extract high 32-bits into x2/w2
*   asr     w3, w2, #shift      // Apply calculated shift 's'
*   add     w0, w3, w0, lsr #31 // Add 1 if dividend is negative to round to 0
* 
* Case 2: MULTIPLY_ADD_SHIFT
* (Example: n / 7 -> M = 0x92492493 (negative), shift = 2)
* ---------------------------------------------------------------------------------
*   smull   x2, w0, w1
*   asr     x2, x2, #32
*   add     w2, w2, w0          // ADD dividend to high half
*   asr     w3, w2, #shift
*   add     w0, w3, w0, lsr #31
* 
* Case 3: MULTIPLY_SUB_SHIFT
* (Example: n / -7 -> M = 0x6db6db6d (positive), shift = 2)
* ---------------------------------------------------------------------------------
*   smull   x2, w0, w1
*   asr     x2, x2, #32
*   sub     w2, w2, w0          // SUBTRACT dividend from high half
*   asr     w3, w2, #shift
*   add     w0, w3, w0, lsr #31
*/

// Handle 0, 1, -1, INT_MIN, power of 2 first.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <cassert>
using int32_t  = int;
using uint32_t = unsigned;

namespace Magic {

enum class MagicStrat {
    MULTIPLY_SHIFT,    
    MULTIPLY_ADD_SHIFT,
    MULTIPLY_SUB_SHIFT 
};

struct MagicNumber {
    int32_t multiplier;
    int32_t shift;
    MagicStrat strat;
};

struct SignedDivisorInfo {
    int32_t value = 0;
    uint32_t magnitude = 0;
    bool reducible = false;
    bool powerOfTwo = false;
    int shift = 0;

    bool usesMagic() const {
        return reducible && magnitude > 1 && !powerOfTwo;
    }
};

inline SignedDivisorInfo analyzeDivisor(int32_t divisor) {
    SignedDivisorInfo info;
    info.value = divisor;
    if (divisor == 0 || divisor == INT32_MIN)
        return info;
    info.reducible = true;
    info.magnitude = divisor < 0
        ? 0u - static_cast<uint32_t>(divisor)
        : static_cast<uint32_t>(divisor);
    info.powerOfTwo = (info.magnitude & (info.magnitude - 1)) == 0;
    if (info.powerOfTwo)
        info.shift = __builtin_ctz(info.magnitude);
    return info;
}

inline MagicNumber getMagic(int32_t d) {
    SignedDivisorInfo info = analyzeDivisor(d);
    assert(info.usesMagic());

    int32_t p = 31;
    uint32_t ad = info.magnitude;
    uint32_t t = 0x80000000U + ((uint32_t)d >> 31);
    uint32_t anc = t - 1 - t % ad;
    uint32_t q1 = 0x80000000U / anc;
    uint32_t r1 = 0x80000000U - q1 * anc;
    uint32_t q2 = 0x80000000U / ad;
    uint32_t r2 = 0x80000000U - q2 * ad;
    uint32_t delta;

    do {
        p = p + 1;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1 = q1 + 1;
            r1 = r1 - anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ad) {
            q2 = q2 + 1;
            r2 = r2 - ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    MagicNumber mag;
    int32_t magic_m = static_cast<int32_t>(q2 + 1);
    
    if (d < 0) {
        magic_m = -magic_m;
    }
    
    mag.multiplier = magic_m;
    mag.shift = p - 32;

    if (d > 0 && magic_m < 0) {
        mag.strat = MagicStrat::MULTIPLY_ADD_SHIFT;
    } else if (d < 0 && magic_m > 0) {
        mag.strat = MagicStrat::MULTIPLY_SUB_SHIFT;
    } else {
        mag.strat = MagicStrat::MULTIPLY_SHIFT;
    }

    return mag;
}

} // namespace Magic
