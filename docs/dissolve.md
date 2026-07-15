# Fade/dissolve transitions (dissolve.h)

True brightness fading isn't achievable on Oric hardware — 8 discrete
colours, no per-pixel luminance. These are the two Oric-appropriate
equivalents, both sourced from a real technique
([OSDK ART15, "Dungeon Master intro"](https://osdk.org/index.php?page=articles&ref=ART15)),
not invented from scratch. Include `dissolve.h`; it auto-compiles
`dissolve.c` via `#pragma compile`, and depends on [hires.h](hires.md).

## Attribute-level dithered fade

```c
void hires_row_colors_range(uint8_t y0, uint8_t y1, uint8_t stride, uint8_t ink, uint8_t paper);
```

Generalizes [hires.md](hires.md)'s `hires_row_colors()` (single row) into
a ranged/strided variant: applies INK/PAPER to rows
`y0, y0+stride, y0+2*stride, ...` up to `y1` (inclusive). `stride=1`
matches a plain ranged `hires_row_colors()` call per row; larger strides
skip rows.

This matches ART15's `ApplyAttributes`/`FancyDitheredFade` technique: call
with a large stride first (e.g. 8), then progressively smaller strides (4,
2, 1) across successive frames, to interlace colour changes in
progressively finer passes — a cheap fade-in that only touches a fraction
of the screen's rows per frame instead of all of them at once.

## Pixel-level dissolve

```c
void hires_dissolve_init(uint16_t seed);
uint16_t hires_dissolve_next(void);
void hires_dissolve_step(const HiresBitmap *hb, uint16_t position, bool set);
```

Uses a 16-bit Galois LFSR (polynomial `x^16+x^14+x^13+x^11+1`, taps
`0xB400`) for a full-period (`2^16-1 = 65535`) pseudo-random sequence with
**O(1) memory** — no 48000-entry permutation table needed for the full
240×200 HIRES pixel space (which a naive shuffle-then-iterate approach
would require).

`hires_dissolve_init(seed)`: seeds the LFSR. **`seed` must not be 0** (a
zero-seeded LFSR never changes state) — `0` is silently substituted with
`1`.

`hires_dissolve_next()`: returns the next pseudo-random pixel index in
`[0, HIRES_WIDTH_PX*HIRES_ROWS)` (0-47999). Rejection-sampled from the
full 65535-value LFSR sequence (out-of-range values, 48000-65535, are
silently skipped) — the in-range subsequence still visits every pixel
exactly once before repeating.

`hires_dissolve_step(hb, position, set)`: decodes a `dissolve_next()`
position into `(x, y)` (`y = position / HIRES_WIDTH_PX`,
`x = position % HIRES_WIDTH_PX`) and calls `hb_put(hb, x, y, set)`.

Typical use: call `hires_dissolve_next()` + `hires_dissolve_step()` some
number of times per frame (however many pixels-per-frame the effect's
budget allows) until the whole screen has been visited — giving a
classic "static-like" full-screen dissolve reveal/hide effect.

**Not currently used by the real demo** — only `src/hires_test.c`'s own
test fixture exercises it (via the Makefile's `MAIN_HIRES_SRCS`), kept
linked in purely for its own regression coverage. A real demo section
built around exactly this technique (`src/section_dissolve_showcase.c`)
was tried and later removed — not because of this library, but because
the section's OWN design never actually called into it (it reimplemented
similar techniques locally, then was redesigned twice more, and
ultimately couldn't achieve a real two-picture crossfade within this
project's memory budget — see `~/.claude/plans/serene-tickling-lemur.md`
and this project's own memory notes for the full history, if resuming
a similar effect later).
