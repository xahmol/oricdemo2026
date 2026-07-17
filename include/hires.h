// hires.h - HIRES-mode bitmap graphics library for Oric Atmos, Oscar64
//
// Mirrors the shape of Oscar64's own include/gfx/bitmap.h, retargeted to the
// Oric's actual HIRES hardware -- see include/oric.h's "HIRES bitmap" and
// "TEXT/HIRES mode-switch attributes" sections for the underlying hardware
// facts this library builds on. Two things make the Oric fundamentally
// different from a C64 bitmap:
//
//   1. 6 pixels per byte, not 8 (bits5-0, bit5 = leftmost; bit6 must stay
//      set on every pixel byte or the ULA reads it as a colour attribute
//      instead -- see oric.h).
//   2. No true per-pixel colour: colour is a byte-level INK/PAPER attribute
//      that applies to the rest of a scanline, plus a per-byte invert bit.
//      There is no equivalent of gfx/mcbitmap.h's per-pixel dithered colour.
//
// Building programs that use this library requires the alternate
// include/oric_crt_hires.c runtime (-rt=include/oric_crt_hires.c), not the
// default include/oric_crt.c -- HIRES mode needs $9800-$BFDF, which
// oric_crt.c's default region layout uses for code/data/stack. See that
// file's header comment for the full memory-layout rationale.

#ifndef HIRES_H
#define HIRES_H

#include <stdint.h>
#include <stdbool.h>
#include "oric.h"

// -------------------------------------------------------------------------
// HiresBitmap -- canvas descriptor
//
// Unlike gfx/bitmap.h's Bitmap (which has a variable cwidth for arbitrary
// off-screen buffers), a HIRES canvas is always 40 bytes wide (one Oric
// scanline) -- only the row count varies, e.g. for an off-screen buffer
// smaller than the full 200-row screen. 'data' is the base address: pass
// HIRESVRAM for the live screen.
// -------------------------------------------------------------------------

typedef struct {
    uint8_t *data;   // base address (HIRESVRAM for the live screen)
    uint8_t  rows;   // canvas height in scanlines (<=HIRES_ROWS for the live screen)
} HiresBitmap;

typedef struct {
    uint8_t top, left, bottom, right;   // clip bounds: left/right in pixel columns, top/bottom in rows
} HiresClip;

// -------------------------------------------------------------------------
// Initialisation
// -------------------------------------------------------------------------

// Build the row/column addressing lookup tables. Call once before any other
// hires_*/hb_* function. Mirrors charwin_init()'s "build via repeated
// addition, not multiply/divide" approach (see hires.c).
void hires_init(void);

// Populate a canvas descriptor. For the live screen: hb_init(&hb, (uint8_t *)HIRESVRAM, HIRES_ROWS).
void hb_init(HiresBitmap *hb, uint8_t *data, uint8_t rows);

// Fill an entire canvas with a raw byte value (e.g. 0x40 for all-paper).
// Real Oric RAM is NOT zero-initialized at power-on -- always fill before
// drawing; don't assume a fresh canvas is blank.
void hb_fill(const HiresBitmap *hb, uint8_t value);

// -------------------------------------------------------------------------
// Canvas scroll. Vertical shifts whole rows (memmove -- correct regardless
// of overlap); vacated rows/columns are filled with fill_value/fill_set.
// NOTE: unlike these, hb_bitblit is NOT safe for overlapping same-canvas
// source/destination regions -- use these dedicated functions to scroll a
// canvas in place, don't try to hb_bitblit a canvas onto itself.
// -------------------------------------------------------------------------

void hb_scroll_up(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_down(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_left(const HiresBitmap *hb, uint8_t amount, bool fill_set);
void hb_scroll_right(const HiresBitmap *hb, uint8_t amount, bool fill_set);

// Byte-aligned (6px-granularity) horizontal scroll: shifts each row's bytes
// by `amount` whole column-bytes via memmove, not per-pixel hb_get/hb_put --
// far cheaper than hb_scroll_left/right for scrolling a full-width canvas
// every frame (that per-pixel cost is the same order of magnitude as
// sprite.h's hb_bitblit-vs-hxspr_draw tradeoff -- see that header's own
// comment). Only ever moves whole bytes, so fine detail can shift by 6px
// steps only; normal, expected granularity for this hardware. Vacated
// column-bytes are filled with the raw `fill_value` byte (e.g. 0x40 for
// blank, or a background tile byte) -- same convention as hb_scroll_up/down.
void hb_scroll_left_fast(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_right_fast(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);

// -------------------------------------------------------------------------
// Pixel primitives
//
// Every pixel-writing primitive unconditionally sets bit6 on the resulting
// byte (marks it as pixel data, not a colour attribute -- see oric.h) and
// never touches bit7 (invert); use hires_invert_byte() for that.
// -------------------------------------------------------------------------

void hb_set(const HiresBitmap *hb, uint8_t x, uint8_t y);
void hb_clr(const HiresBitmap *hb, uint8_t x, uint8_t y);
bool hb_get(const HiresBitmap *hb, uint8_t x, uint8_t y);
void hb_put(const HiresBitmap *hb, uint8_t x, uint8_t y, bool set);

// -------------------------------------------------------------------------
// Lines
// -------------------------------------------------------------------------

// Draw an unclipped line.
void hbu_line(const HiresBitmap *hb, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set);

// Draw a clipped line.
void hb_line(const HiresBitmap *hb, const HiresClip *clip, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set);

// -------------------------------------------------------------------------
// Mode switching -- see oric.h's "TEXT/HIRES mode-switch attributes" and the
// design-rationale comment above hires_on() in hires.c for the underlying
// hardware mechanism (the switch attribute is STICKY: written once, it
// re-triggers every frame with no per-frame re-poking needed).
// -------------------------------------------------------------------------

// Switch to HIRES mode by pokeing the switch attribute at the last column
// of TEXT row 0. Caller should hb_fill()/draw the HIRES canvas beforehand
// (real RAM is not zero-initialized -- see hb_fill()). pal50hz: true for
// PAL Oric Atmos (Europe, the common case), false for NTSC.
//
// Also blanks the HIRES-mode charset banks ($9800-$9FFF -- see oric.h's
// HIRES_CHARSET_STD/ALT). This matters even if the caller never draws
// text: the Oric's bottom 24 scanlines (3 TEXT rows, $BF68-$BFDF) are
// UNCONDITIONALLY rendered as TEXT mode regardless of hires_footer_enable()
// -- HIRES bitmap only ever covers 200 scanlines, so those last 24 have no
// HIRES rendering path at all on real hardware. Whatever's currently in
// $BF68-$BFDF gets interpreted as character codes against whichever
// charset bank is "active" for TEXT-mode lookups -- which, once HIRES mode
// has ever been entered, is $9800/$9C00 (not the ordinary TEXT-mode
// $B400/$B800), since nothing else in this codebase changes that back.
// Nothing else in this codebase ever populates $9800/$9C00 with real
// glyph bitmaps (no HIRES-mode caller has needed anything beyond blank),
// and real Oric RAM is NOT zero-initialized at power-on (same fact
// hb_fill() warns about) -- without this, that bottom border renders
// whatever undefined bytes happen to be there as visible noise, seen as
// a solid white bar running the real demo in Oricutron (Phosphoric
// apparently zero-fills RAM more favourably, masking this). Caller still
// needs to fill $BF68-$BFDF itself with a blank character code (e.g.
// CH_SPACE) -- see hires_footer_enable()'s own doc comment for the
// separate, unrelated caveat about that function specifically.
void hires_on(bool pal50hz);

// Clear the sticky HIRES-entry trigger, reverting fully to a normal 28-row
// TEXT screen from the next frame onward. Writes CH_SPACE at TEXTVRAM's
// last column -- caller owns whatever else belongs in that screen cell.
void hires_off(void);

// Enable the built-in 3-row TEXT footer (rows 25-27 of the normal 28-row
// TEXT geometry) at the bottom of a HIRES screen, without leaving HIRES
// mode for rows 0-199 above it. Costs the last byte of the HIRES bitmap
// ($BF3F) -- it can no longer hold pixel data while the footer is enabled.
//
// IMPORTANT CAVEAT, confirmed against Oricutron's own ULA source
// (ula.c's ula_decode_attr(), case 0x18): switching a scanline to TEXT
// mode via the serial video-mode attribute ALSO switches the glyph-lookup
// charset base back to the STANDARD TEXT location ($B400 for the
// standard set, $B800 for the alternate) -- NOT the HIRES-mode-relocated
// $9800/$9C00 banks, despite oric_atmos_reference.md's "character sets
// move to $9800/$9C00 when HIRES active" framing (true only for scanlines
// actively rendering HIRES pixels, not for TEXT-mode scanlines that
// happen to occur while overall still "in HIRES mode" -- the footer is
// the latter). $B400-$BBFF is itself INSIDE the live HIRES bitmap's own
// address range (HIRESVRAM+128*40 through +204*40 -- roughly HIRES rows
// 128-199) -- so glyph data for the footer's characters is read from
// whatever pixel/attribute bytes your own HIRES drawing has left in that
// portion of the screen. This is a real, unavoidable hardware aliasing,
// not a missing-initialization bug (confirmed: blanking $9800/$9C00, the
// wrong bank, does nothing; there is no way to give $B400-$BBFF stable
// "glyph" content without it also being live bitmap content, short of
// never drawing anything in HIRES rows 128-199). Caller must either
// avoid drawing in that row range entirely, or accept that footer
// characters will show transient noise reflecting whatever's currently
// drawn there. See project memory for the investigation that found this
// (reported as "corruption in the lower border" running the real demo in
// Oricutron, not reproduced in Phosphoric).
void hires_footer_enable(bool pal50hz);

// Disable the footer, restoring the last HIRES byte to ordinary (blank)
// pixel data. Rows 200-223 then show undefined content until redrawn.
void hires_footer_disable(void);

// -------------------------------------------------------------------------
// Attribute/colour API
//
// col_byte is a column-BYTE index (0-39), not a pixel x coordinate --
// attributes are byte-level, no sub-byte position. Pass an A_FW*/A_BG*
// constant from oric.h directly (written as given, no masking, same
// convention as charwin.c's cwin_put_attr()). hires_put_ink/put_paper/
// row_colors operate on the live screen only; hires_invert_byte() takes a
// HiresBitmap* since the invert bit is ordinary pixel data that survives a
// later bitblit from an off-screen canvas.
// -------------------------------------------------------------------------

void hires_put_ink(uint8_t col_byte, uint8_t y, uint8_t ink);
void hires_put_paper(uint8_t col_byte, uint8_t y, uint8_t paper);

// Sets INK at column-byte 0 and PAPER at column-byte 1 of row y (same
// column order as charwin.c's row_setattr()). CAVEAT: the ULA resets to
// hardware-default white-ink/black-paper at the start of every scanline,
// before it has scanned any attribute byte on that line -- so column-byte
// 0's own on-screen cell (the ink byte itself) always renders against
// black paper, REGARDLESS of the `paper` argument passed here, since
// paper only takes effect starting at column-byte 1 where it's actually
// written. This is invisible whenever `paper` is A_BGBLACK (matches the
// hardware default anyway -- true for every caller except
// section_background.c), but produces a real 6px mis-coloured stripe at
// column 0 for any row using a non-black paper -- see section_background.c
// for the fix (call hires_put_paper() directly at column-byte 0 instead,
// skipping this function, whenever ink doesn't actually need to change
// from the hardware default).
void hires_row_colors(uint8_t y, uint8_t ink, uint8_t paper);

// Toggle the invert bit (bit7) of a single byte, without touching its
// pixel bits or bit6.
void hires_invert_byte(const HiresBitmap *hb, uint8_t xbyte, uint8_t y, bool on);

// -------------------------------------------------------------------------
// AIC (Alternate Inverted Colors) -- a different ink/paper pair on even
// vs. odd scanlines (ink[0]/paper[0] = even rows, ink[1]/paper[1] = odd
// rows), so eye/CRT blending of alternating lines fakes extra perceived
// colours (popularised in games like Pulsoids). See hires.c for the full
// rationale.
// -------------------------------------------------------------------------

typedef struct {
    uint8_t ink[2];
    uint8_t paper[2];
} HiresAIC;

void hires_aic_init(HiresAIC *aic, uint8_t ink0, uint8_t paper0, uint8_t ink1, uint8_t paper1);
void hires_aic_apply_row(const HiresAIC *aic, uint8_t y);
void hires_aic_apply_range(const HiresAIC *aic, uint8_t y0, uint8_t y1);

// -------------------------------------------------------------------------
// Rect / circle / triangle / polygon fill
//
// All built on hb_get/hb_put (simple per-pixel loops), not hand-optimized
// byte-level bit-shifting -- correctness first; see hires.c.
// -------------------------------------------------------------------------

// Fills row-by-row, top to bottom; WITHIN each row, right to left (column
// w-1 down to 0) -- see hires.c's own comment for why: on a large,
// unsynchronized fill (no vsync wait anywhere in this project), the CPU
// visibly races the CRT beam, so whichever end of a row is touched FIRST
// shows wrong/blank for a while before the fill catches up. Right-to-left
// means column-bytes 0-1 (a row's own ink/paper attribute bytes, whenever
// a fill spans x=0) are touched LAST, not first.
void hb_rect_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool set);

// Monochrome pattern fill: repeats an 8x8 pixel tile (pattern[8], one byte
// per tile row, bit7=leftmost) across a w x h rect. Hatching/texture fill,
// not colour dithering -- Oric has no true per-pixel colour to dither into.
void hb_rect_pattern(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t pattern[8]);

// Filled ellipse, centre (cx,cy), radii (rx,ry).
void hb_ellipse_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t rx, uint8_t ry, bool set);

// Filled circle, centre (cx,cy), radius r -- thin wrapper over hb_ellipse_fill.
void hb_circle_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t r, bool set);

// No general hb_polygon_fill()/hb_triangle_fill() -- REMOVED 2026-07-15.
// hires.c's own per-pixel x per-edge point-in-polygon test had TWO real
// problems: (1) a documented, unresolved Oscar64 -O2 whole-program
// register-allocator bug that silently dropped most of its fill loop's
// iterations at SOME call sites, resistant to every previously-tried fix
// (~/.claude/oscar64.md's "Third symptom shape" entry); (2) even when it
// DID run correctly, a division inside the innermost per-pixel loop made
// it take several real SECONDS to fill an ordinary-sized shape on this
// 1MHz CPU (confirmed via Phosphoric frame-dumps showing a star visibly
// growing over that whole time, in both src/section_hires_showcase.c's
// and src/section_rasterirq_showcase.c's own stars). Every real call site
// in this project has since moved to the pattern below, which has
// neither problem. If a future shape genuinely can't be flood-filled this
// way (e.g. a seed point can't be found reliably inside it), reach for
// hb_rect_fill()/hb_ellipse_fill() combinations first; only reintroduce a
// general polygon fill as a last resort, and re-verify BOTH problems
// above are actually gone before trusting it anywhere real.
//
// RECOMMENDED PATTERN for a filled arbitrary shape: draw the shape's own
// closed outline via repeated hb_line() calls (one per edge, wrapping
// back to the first vertex), then hb_flood_fill() from a point known to
// be inside it (e.g. a symmetric shape's own centre/centroid) -- see
// section_hires_showcase.c's SHOW_STAR state or
// section_rasterirq_showcase.c's draw_star() for two worked examples.
// This needs the caller to supply a valid interior seed point (not
// automatic for an arbitrary/non-convex shape the way the old point-in-
// polygon test was), but is dramatically faster (hb_flood_fill() is a
// scanline-stack algorithm, no per-pixel division at all -- see its own
// header comment in hires.c) and has none of the miscompilation history
// above.

// Flood fill (paint bucket), non-recursive scanline-stack algorithm (see
// hires.c). If the fixed-size internal seed stack fills up, the flood
// simply stops spreading past that point rather than crashing or
// overflowing -- a degraded (partial) result, not a memory-safety issue.
void hb_flood_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, bool set);

// -------------------------------------------------------------------------
// Bitblit -- no pattern-fill variant (unlike gfx/bitmap.c's BlitOp): Oric
// has no true per-pixel colour to dither a pattern into.
// -------------------------------------------------------------------------

typedef enum {
    HBLIT_COPY,   // destination = source
    HBLIT_OR,     // destination |= source
    HBLIT_AND,    // destination &= source
    HBLIT_XOR     // destination ^= source
} HiresBlitOp;

void hb_bitblit(const HiresBitmap *dst, const HiresClip *clip, uint8_t dx, uint8_t dy,
                 const HiresBitmap *src, uint8_t sx, uint8_t sy, uint8_t w, uint8_t h, HiresBlitOp op);

// -------------------------------------------------------------------------
// Text -- renders using a copy of the Oric ROM's standard 6x8 charset
// held in HIRES_CHARSET_STD (NOT live CHARSETROM -- see hires.c's own
// comment for why: unreliable once ROM gets banked out/on targets with no
// real ROM at all). Addressed as a full 128-entry table (`code*8`, from
// code 0) -- matching real charset RAM layout, NOT CHARSETROM's own
// 0-based-at-0x20, 96-entry convention -- so the caller's own boot-time
// copy must place glyphs at their own `code*8` offset (printable ASCII
// starts at byte 0x100, not byte 0). Caller must copy real font data into
// HIRES_CHARSET_STD once at boot (typically from a compiled-in asset)
// before first use -- see main.c. Printable ASCII 0x20-0x7F only. Returns
// the pixel width consumed.
// -------------------------------------------------------------------------

int hb_put_chars(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, const char *str, uint8_t len);

// Alignment helpers, mirroring ttf_print_left/center/right -- using
// hb_put_chars's fixed 6px/glyph width (len*6), not ttf_strlen().
int hb_put_chars_left(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);
int hb_put_chars_center(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);
int hb_put_chars_right(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);

#pragma compile("hires.c")

#endif // HIRES_H
