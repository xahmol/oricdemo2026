// ttf.h - Proportional bitmap font rendering for Oric HIRES mode
//
// API shape (a TtfFont descriptor: fixed height, per-glyph pixel width,
// per-glyph byte width, per-glyph offset into a shared glyph blob, plus
// alignment-aware print helpers) is inspired by raxiss/iss000's lib-ttf
// (github.com/iss000/oricOpenLibrary/tree/main/lib-ttf) -- credit for the
// *concept*, not a port: lib-ttf's own runtime is hand-written 6502
// assembly fed by a Lua + ImageMagick offline pipeline; this is plain
// Oscar64 C reusing hires.c's hb_put()-based per-pixel rendering (the same
// approach as hb_put_chars()), fed by tools/oric_ttfconv.py (pure Python +
// Pillow, not Lua/ImageMagick). The packed glyph-data format below is this
// project's own convention (6px/byte, bit5=leftmost, local to each glyph),
// not lib-ttf's assembly data layout.

#ifndef TTF_H
#define TTF_H

#include <stdint.h>
#include "hires.h"

typedef struct {
    uint8_t w;                     // max glyph width in pixels (informational)
    uint8_t h;                     // glyph height in pixels (rows), same for every glyph
    uint8_t first_char, last_char; // ASCII code range covered
    const uint8_t *widths;         // per-glyph pixel width, indexed by (code - first_char)
    const uint8_t *width_bytes;    // per-glyph byte width = ceil(width/6)
    const uint16_t *offsets;       // per-glyph byte offset into char_defs
    const uint8_t *char_defs;      // packed glyph rows, 6px/byte, bit5=leftmost (glyph-local)
} TtfFont;

extern uint8_t ttf_space;   // extra inter-character spacing in pixels (default 1)

// Total rendered pixel width of a string under the given font (sum of
// glyph widths + ttf_space between characters); characters outside
// [first_char, last_char] are skipped (zero width).
uint16_t ttf_strlen(const TtfFont *font, const char *s);

// Render a string at pixel position (x,y). Returns the pixel width consumed.
uint16_t ttf_print(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t x, uint8_t y, const char *s);

// Convenience alignment helpers over ttf_print/ttf_strlen -- left/centre/
// right align within the full HIRES_WIDTH_PX-wide screen.
uint16_t ttf_print_left(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s);
uint16_t ttf_print_center(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s);
uint16_t ttf_print_right(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s);

#pragma compile("ttf.c")

#endif // TTF_H
