#pragma once
#include <cstdint>
#include <cassert>
using int32_t = int;
using uint32_t = unsigned int;

inline void GetSignedMagic(int32_t d, uint32_t &magic, unsigned &shift, bool &negMagic) {
    assert(d > 1);
    const uint32_t two31 = 0x80000000u;
    uint32_t ad = static_cast<uint32_t>(d);
    uint32_t t = two31;                     // d > 0 时的高位调整
    uint32_t anc = t - 1 - t % ad;
    unsigned p = 31;

    uint32_t q1 = two31 / anc;
    uint32_t r1 = two31 - q1 * anc;
    uint32_t q2 = two31 / ad;
    uint32_t r2 = two31 - q2 * ad;
    uint32_t delta = 0;
    do {
        ++p;
        q1 <<= 1; r1 <<= 1;
        if (r1 >= anc) { ++q1; r1 -= anc; }
        q2 <<= 1; r2 <<= 1;
        if (r2 >= ad)  { ++q2; r2 -= ad; }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    magic = q2 + 1;
    shift = p - 32;
    negMagic = (magic & two31) != 0;        // 魔数作为有符号32位是否为负
}