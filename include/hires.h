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
void hires_on(bool pal50hz);

// Clear the sticky HIRES-entry trigger, reverting fully to a normal 28-row
// TEXT screen from the next frame onward. Writes CH_SPACE at TEXTVRAM's
// last column -- caller owns whatever else belongs in that screen cell.
void hires_off(void);

// Enable the built-in 3-row TEXT footer (rows 25-27 of the normal 28-row
// TEXT geometry) at the bottom of a HIRES screen, without leaving HIRES
// mode for rows 0-199 above it. Costs the last byte of the HIRES bitmap
// ($BF3F) -- it can no longer hold pixel data while the footer is enabled.
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
// column order as charwin.c's row_setattr()).
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

void hb_rect_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool set);

// Filled circle, centre (cx,cy), radius r.
void hb_circle_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t r, bool set);

// Filled polygon (even-odd rule -- handles convex and simple concave
// polygons alike). xs/ys are parallel arrays of num vertices.
void hb_polygon_fill(const HiresBitmap *hb, const HiresClip *clip, const uint8_t *xs, const uint8_t *ys, uint8_t num, bool set);

// Filled triangle -- convenience wrapper over hb_polygon_fill.
void hb_triangle_fill(const HiresBitmap *hb, const HiresClip *clip,
                       uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, bool set);

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
// Text -- renders using the Oric ROM's standard 6x8 charset (CHARSETROM),
// printable ASCII 0x20-0x7F only. Returns the pixel width consumed.
// -------------------------------------------------------------------------

int hb_put_chars(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, const char *str, uint8_t len);

#pragma compile("hires.c")

#endif // HIRES_H
