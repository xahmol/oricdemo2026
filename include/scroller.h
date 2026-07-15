// scroller.h - reusable HIRES right-to-left text scroller.
//
// Round 12 redesign, per explicit user feedback ("smooth scroll is not
// smooth if too slow, so indeed just scroll per char"): the original
// design re-rendered the string at a new, DECREMENTING PIXEL x each tick
// via ttf_print()/hb_put_chars() -- smooth 1-2px motion, but expensive
// (every visible pixel of every glyph tested/set individually, twice per
// tick: once to erase the old frame, once to draw the new one).
//
// This version moves in whole COLUMN-BYTE (6px) steps instead, and
// copies each visible character's raw glyph bytes DIRECTLY into the
// screen's HIRES bitmap data -- no per-pixel hb_put() calls at all.
// This is only possible because the ROM standard charset (hires.c's own
// hb_put_chars(), HIRES_CHARSET_STD) is EXACTLY 6px wide, matching one
// HIRES column-byte exactly, and its glyph bytes are already bit-aligned
// to match HIRES pixel-byte layout (bit5 = leftmost pixel) -- so moving
// in 6px steps means every character always lands exactly on a byte
// boundary, and one raw byte write (glyph byte | 0x40, see hires.c's own
// header comment on why bit6 must be set) does the whole job: draws the
// new content AND overwrites/erases whatever was there before in one
// step, with no separate erase pass needed at all.
//
// The real, named tradeoff: motion is now blockier (6px per step)
// instead of smooth (1-2px per step) -- but the OLD "smooth" motion was
// slow enough to not read as smooth in practice (per the user's own
// report), so this trades perceived smoothness for real speed, which is
// the actual win here.
//
// Follow-up: 6px EVERY tick then read as too fast to comfortably read --
// scroller.c's own SCROLLER_TICKS_PER_STEP now throttles how often the
// column advances (a plain tick delay, not a smaller step size -- see
// that constant's own comment for why a genuine sub-byte 2-3px step was
// considered and rejected).
//
// Caller owns the row band's own colour setup (hires_row_colors()) --
// this module only ever touches pixel content. Two styles (SCROLLER_PLAIN/
// SCROLLER_BOUNCE) share the same engine, parameterized so
// src/section_scroll_showcase.c, src/section_macaw_showcase.c, and
// src/section_credits.c all reuse it with different text/style.

#ifndef SCROLLER_H
#define SCROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "hires.h"

// Skip column-bytes 0-1 (12px) -- the row's own baseline ink/paper
// attribute bytes -- same convention as every other section's own
// SAT_MIN_COL/CAPTION_X-style margin.
#define SCROLLER_MIN_COL 2u

// Fixed glyph size of hires.c's own hb_put_chars() (the ROM standard
// charset) -- 6px wide (one column-byte), 8px tall, monospaced.
#define SCROLLER_GLYPH_W 6u
#define SCROLLER_GLYPH_H 8u

typedef enum {
    SCROLLER_PLAIN,   // straight horizontal scroll at a fixed row
    SCROLLER_BOUNCE   // oric_sin()-driven vertical bounce while scrolling
} ScrollerStyle;

// Begins a scrolling pass: `text` starts just off the right edge of the
// screen and scrolls left one column-byte (6px) at a time at `y`
// (SCROLLER_BOUNCE oscillates a few pixels above/below `y` instead of
// holding it fixed), until it has fully scrolled off the left edge.
// `text` must remain valid (e.g. a string literal) for the whole
// scroll's duration -- only a pointer is kept, not a copy. `y` should
// leave SCROLLER_GLYPH_H rows (plus bounce headroom, if used) of
// genuinely blank/reserved space -- this module does not know what's
// underneath and will overwrite it.
void scroller_init(const HiresBitmap *screen, const char *text, uint8_t y, ScrollerStyle style);

// Advances the scroll by one column-byte (6px) step (draws the whole
// visible band fresh, which also erases the previous frame -- see this
// file's own header comment). Returns true once the text has fully
// scrolled off the left edge (the caller should stop calling
// scroller_tick() after this, typically by calling section_mark_finished()).
bool scroller_tick(const HiresBitmap *screen);

#pragma compile("scroller.c")

#endif // SCROLLER_H
