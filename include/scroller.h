// scroller.h - reusable HIRES right-to-left text scroller, built on ttf.h
//
// Re-renders the string at a new, decrementing x each tick (erase old
// bounding box, draw new one) rather than shifting existing pixel data
// via hb_scroll_left_fast() -- avoids that primitive's own "must not drag
// column-bytes 0-1 (the row's own baseline ink/paper attribute bytes)
// into the shift" hazard (the same one section_clouds.c's own
// clouds_scroll_left() works around) by simply never touching those
// columns in the first place: every draw/erase call is clipped to
// SCROLLER_MIN_X..HIRES_WIDTH_PX-1. Coordinates are plain uint8_t and
// rely on natural 8-bit wraparound + HiresClip rejection (see ttf.c's own
// px = (uint8_t)(x + col) pattern) to correctly stop drawing once the
// text has scrolled off either edge -- no special-casing needed for
// "partially/fully off-screen".
//
// Caller owns the row band's own colour setup (hires_row_colors()) --
// this module only ever touches pixel content. Two styles (SCROLLER_PLAIN/
// SCROLLER_BOUNCE) share the same engine, parameterized so
// src/section_scroll_showcase.c and (later) src/section_credits.c can
// reuse it with different text/style.

#ifndef SCROLLER_H
#define SCROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "hires.h"
#include "ttf.h"

// Skip column-bytes 0-1 (12px) -- the row's own baseline ink/paper
// attribute bytes -- same convention as every other section's own
// SAT_MIN_COL/CAPTION_X-style margin.
#define SCROLLER_MIN_X 12u

typedef enum {
    SCROLLER_PLAIN,   // straight horizontal scroll at a fixed row
    SCROLLER_BOUNCE   // oric_sin()-driven vertical bounce while scrolling
} ScrollerStyle;

// Begins a scrolling pass: `text` starts just off the right edge of the
// screen and scrolls left at `y` (SCROLLER_BOUNCE oscillates a few pixels
// above/below `y` instead of holding it fixed), advancing SCROLLER_STEP
// (see scroller.c) pixels per scroller_tick() call, until it has fully
// scrolled off the left edge. `text` must remain valid (e.g. a string
// literal) for the whole scroll's duration -- only a pointer is kept, not
// a copy.
void scroller_init(const HiresBitmap *screen, const TtfFont *font, const char *text, uint8_t y, ScrollerStyle style);

// Advances the scroll by one step (erase the old position, draw the new
// one). Returns true once the text has fully scrolled off the left edge
// (the caller should stop calling scroller_tick() after this, typically
// by calling section_mark_finished()).
bool scroller_tick(const HiresBitmap *screen);

#pragma compile("scroller.c")

#endif // SCROLLER_H
