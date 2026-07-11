// section_splash.h - idi8b brand splash (HIRES mode, per-cell dissolve)
//
// The demo's first HIRES-mode section: the "idi8b" mosaic wordmark,
// reproduced faithfully from idi8b_logo[]'s own original TEXT-mode data
// via a HIRES_CHARSET_ALT glyph lookup (see section_splash.c's own header
// comment for the full mechanism, and for why this section moved from
// TEXT to HIRES mode in the first place), dissolves in cell-by-cell,
// holds along with "IDreamtIn8Bits.com" (standard charset) and a
// hand-written-script "presents...." caption (TTF), then dissolves back
// out.
//
// Like every other section, this one's own _tick() naturally reaches a
// finished state (the fade-out completes) well before any caller-imposed
// time limit -- see main.c's DemoSection.tick return value.

#ifndef SECTION_SPLASH_H
#define SECTION_SPLASH_H

#include <stdbool.h>
#include "hires.h"

// One-time setup: blanks the canvas, establishes the bar colours, and
// resets the fade state machine to its starting (fade-in) state.
void section_splash_init(const HiresBitmap *screen);

// One fade step. Returns true once the fade-out has fully completed (the
// screen is blank again) -- the caller (main.c's run_section()) advances
// to the next section immediately when this returns true, regardless of
// its own min_ticks/max_ticks bookkeeping.
bool section_splash_tick(const HiresBitmap *screen);

#pragma compile("section_splash.c")

#endif // SECTION_SPLASH_H
