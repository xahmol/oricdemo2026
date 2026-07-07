// dissolve.h - Fade/dissolve transitions for HIRES mode
//
// True brightness fading isn't achievable on Oric hardware (8 discrete
// colours, no per-pixel luminance) -- these are the two Oric-appropriate
// equivalents, both sourced from real techniques (OSDK ART15, "Dungeon
// Master intro"), not invented from scratch:
//
//   1. hires_row_colors_range() -- an attribute-level dithered fade,
//      generalizing hires.h's hires_row_colors() (single row) into a
//      ranged/strided variant matching ART15's ApplyAttributes/
//      FancyDitheredFade: call with a large stride (e.g. 8) first, then
//      smaller strides (4, 2, 1) across successive frames, to interlace
//      colour changes in progressively finer passes -- a cheap fade-in.
//   2. hires_dissolve_* -- a pixel-level dissolve using a 16-bit Galois
//      LFSR for a full-period pseudo-random pixel-reveal sequence with
//      O(1) memory (no 48000-entry permutation table for the full
//      240x200 pixel space).

#ifndef DISSOLVE_H
#define DISSOLVE_H

#include <stdint.h>
#include <stdbool.h>
#include "hires.h"

// Applies INK/PAPER to rows y0, y0+stride, y0+2*stride, ... up to y1
// (inclusive). stride=1 matches a plain ranged hires_row_colors(); larger
// strides skip rows for a cheap interlaced/dithered fade look.
void hires_row_colors_range(uint8_t y0, uint8_t y1, uint8_t stride, uint8_t ink, uint8_t paper);

// 16-bit Galois LFSR (polynomial x^16+x^14+x^13+x^11+1, taps 0xB400) --
// full period 2^16-1 = 65535. seed MUST NOT be 0 (a zero-seeded LFSR never
// changes state); hires_dissolve_init() substitutes 1 if given 0.
void hires_dissolve_init(uint16_t seed);

// Next pseudo-random pixel index in [0, HIRES_WIDTH_PX*HIRES_ROWS) --
// rejection-sampled from the full 65535-value LFSR sequence (out-of-range
// values are silently skipped), so the in-range subsequence still visits
// every pixel exactly once before repeating.
uint16_t hires_dissolve_next(void);

// Decode a dissolve_next() position into (x,y) and hb_put() it.
void hires_dissolve_step(const HiresBitmap *hb, uint16_t position, bool set);

#pragma compile("dissolve.c")

#endif // DISSOLVE_H
