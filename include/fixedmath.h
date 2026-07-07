// fixedmath.h - Fixed-point sine/cosine lookup table for fast demo effects
//
// Generic, not HIRES-specific -- usable by any TEXT-mode or HIRES-mode
// effect (plasma, sinus scrollers, rotozoomers) that needs many sin/cos
// evaluations per frame. Runtime float sin()/cos() (include/crt_math.c) is
// far too slow for that -- a single 256-entry table lookup is the standard
// fast-path technique on 6502.

#ifndef FIXEDMATH_H
#define FIXEDMATH_H

#include <stdint.h>

// sin_table[a] = round(127 * sin(2*pi*a/256)), a = 0-255 "angle units"
// (256 units = one full turn). Range -127..127.
extern const int8_t sin_table[256];

// oric_sin/oric_cos: table lookup, O(1). oric_cos reuses sin_table via a
// 64-unit (90 degree) phase shift (cos(x) = sin(x+90deg); 64/256 = 90/360)
// rather than a second table.
inline int8_t oric_sin(uint8_t angle)
{
    return sin_table[angle];
}

inline int8_t oric_cos(uint8_t angle)
{
    return sin_table[(uint8_t)(angle + 64)];
}

#pragma compile("fixedmath.c")

#endif // FIXEDMATH_H
