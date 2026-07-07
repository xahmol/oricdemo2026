# Fixed-point sine/cosine table (fixedmath.h)

A 256-entry lookup table for fast sine/cosine evaluation — generic, not
HIRES-specific, usable by any TEXT-mode or HIRES-mode effect (plasma,
sinus scrollers, rotozoomers) that needs many sin/cos evaluations per
frame. Runtime float `sin()`/`cos()` (`include/crt_math.c`) is far too slow
for that; a single table lookup is the standard fast-path technique on
6502. Include `fixedmath.h`; it auto-compiles `fixedmath.c` via
`#pragma compile`.

```c
extern const int8_t sin_table[256];

inline int8_t oric_sin(uint8_t angle);
inline int8_t oric_cos(uint8_t angle);
```

`sin_table[a] = round(127 * sin(2*pi*a/256))`, for `a` = 0-255 "angle
units" (256 units = one full turn, matching the classic 8-bit convention
of an angle byte instead of degrees/radians). Range `-127..127`.

`oric_sin(angle)` is a direct table lookup, `O(1)`. `oric_cos(angle)`
reuses the *same* table via a 64-unit (90°) phase shift
(`cos(x) = sin(x+90°)`; `64/256 = 90/360`) rather than a second table —
halves the static data cost (256 bytes total, not 512).

The table itself is generated once by a one-off Python snippet (not a
checked-in CLI tool, unlike `oric_pictconv.py`/`oric_ttfconv.py` — this is
static data with fixed parameters, not something meant to be regenerated
with different arguments) and checked in as plain C source
(`include/fixedmath.c`).
