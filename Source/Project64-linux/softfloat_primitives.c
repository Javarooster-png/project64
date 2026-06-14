#include <stdint.h>
#include <primitiveTypes.h>

uint_fast8_t softfloat_countLeadingZeros32(uint32_t a)
{
    return a ? __builtin_clz(a) : 32;
}

uint_fast8_t softfloat_countLeadingZeros64(uint64_t a)
{
    return a ? __builtin_clzll(a) : 64;
}

struct uint128 softfloat_mul64To128(uint64_t a, uint64_t b)
{
    union
    {
        unsigned __int128 ui;
        struct uint128 s;
    } uZ;

    uZ.ui = (unsigned __int128)a * b;
    return uZ.s;
}
