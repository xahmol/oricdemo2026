// hires.c - HIRES-mode bitmap graphics library implementation. See hires.h.

#include <string.h>
#include "hires.h"

// -------------------------------------------------------------------------
// Addressing tables -- built once by hires_init(), mirroring charwin_init()'s
// "build via repeated addition to avoid a 6502 multiply/divide" approach.
//
//   hires_row_off[y]   = y * HIRES_ROW_BYTES    (byte offset of row y from a canvas base)
//   hires_col_byte[x]  = x / 6                  (byte offset within a row)
//   hires_col_mask[x]  = 0x20 >> (x % 6)        (pixel bit within that byte, bit5=leftmost)
//
// Offsets (not absolute addresses) so the same tables serve any HiresBitmap
// canvas, not just the live HIRESVRAM screen -- unlike charwin_init()'s
// row_base[], which only ever needs to address the single fixed TEXTVRAM
// screen. 400 + 240 + 240 = 880 bytes total. Every pixel-level primitive is
// then a table lookup + add, no multiply/divide, no runtime modulo.
// -------------------------------------------------------------------------

static uint16_t hires_row_off[HIRES_ROWS];
static uint8_t  hires_col_byte[HIRES_WIDTH_PX];
static uint8_t  hires_col_mask[HIRES_WIDTH_PX];

void hires_init(void)
{
    uint16_t off = 0;
    for (uint8_t y = 0; y < HIRES_ROWS; y++)
    {
        hires_row_off[y] = off;
        off = (uint16_t)(off + HIRES_ROW_BYTES);
    }

    uint8_t byte_idx = 0;
    uint8_t bit_idx = 0;
    for (uint16_t x = 0; x < HIRES_WIDTH_PX; x++)
    {
        hires_col_byte[x] = byte_idx;
        hires_col_mask[x] = (uint8_t)(0x20 >> bit_idx);
        bit_idx++;
        if (bit_idx == 6)
        {
            bit_idx = 0;
            byte_idx++;
        }
    }
}

void hb_init(HiresBitmap *hb, uint8_t *data, uint8_t rows)
{
    hb->data = data;
    hb->rows = rows;
}

// Fill an entire canvas with a raw byte value (e.g. 0x40 to clear to
// all-paper). Real Oric RAM is NOT zero-initialized at power-on, so a fresh
// HiresBitmap's contents are garbage, not blank -- always fill before
// drawing, same as gfx/bitmap.h's bm_fill().
void hb_fill(const HiresBitmap *hb, uint8_t value)
{
    memset(hb->data, value, (unsigned)hb->rows * HIRES_ROW_BYTES);
}

// -------------------------------------------------------------------------
// Pixel primitives
//
// Every pixel-writing primitive unconditionally ORs in bit6 (0x40) on the
// resulting byte: a byte that happens to land in 0-31 after a naive
// bitwise pixel op would otherwise be silently reinterpreted by the ULA as
// a colour attribute instead of pixel data (see oric.h's HIRES bitmap
// section). bit7 (invert) is never touched here -- that's the separate
// hires_invert_byte() API, so "set pixels" and "invert rendering" compose
// independently, matching how the hardware treats them as orthogonal bits.
// -------------------------------------------------------------------------

void hb_set(const HiresBitmap *hb, uint8_t x, uint8_t y)
{
    uint8_t *p = hb->data + hires_row_off[y] + hires_col_byte[x];
    *p |= (uint8_t)(0x40 | hires_col_mask[x]);
}

#pragma native(hb_set)

void hb_clr(const HiresBitmap *hb, uint8_t x, uint8_t y)
{
    uint8_t *p = hb->data + hires_row_off[y] + hires_col_byte[x];
    *p = (uint8_t)((*p | 0x40) & ~hires_col_mask[x]);
}

#pragma native(hb_clr)

bool hb_get(const HiresBitmap *hb, uint8_t x, uint8_t y)
{
    uint8_t *p = hb->data + hires_row_off[y] + hires_col_byte[x];
    return (*p & hires_col_mask[x]) != 0;
}

#pragma native(hb_get)

void hb_put(const HiresBitmap *hb, uint8_t x, uint8_t y, bool set)
{
    if (set)
        hb_set(hb, x, y);
    else
        hb_clr(hb, x, y);
}

#pragma native(hb_put)

// -------------------------------------------------------------------------
// Lines -- standard integer Bresenham. hb_line() shares the same stepping
// loop as hbu_line() via _hb_line_core(), only skipping the hb_put() call
// for points outside the clip rect (rather than a true Cohen-Sutherland/
// Liang-Barsky pre-clip) -- simplest correct approach at HIRES's small
// (240x200) resolution.
// -------------------------------------------------------------------------

static void _hb_line_core(const HiresBitmap *hb, const HiresClip *clip,
                           uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set)
{
    int16_t dx = (int16_t)x1 - (int16_t)x0;
    int16_t dy = (int16_t)y1 - (int16_t)y0;
    int16_t sx = (dx < 0) ? -1 : 1;
    int16_t sy = (dy < 0) ? -1 : 1;
    if (dx < 0) dx = (int16_t)(-dx);
    if (dy < 0) dy = (int16_t)(-dy);
    int16_t err = (int16_t)(dx - dy);
    int16_t x = x0, y = y0;

    for (;;)
    {
        if (!clip || (x >= clip->left && x <= clip->right && y >= clip->top && y <= clip->bottom))
            hb_put(hb, (uint8_t)x, (uint8_t)y, set);

        if (x == x1 && y == y1)
            break;

        int16_t e2 = (int16_t)(2 * err);
        if (e2 > -dy) { err = (int16_t)(err - dy); x = (int16_t)(x + sx); }
        if (e2 <  dx) { err = (int16_t)(err + dx); y = (int16_t)(y + sy); }
    }
}

void hbu_line(const HiresBitmap *hb, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set)
{
    _hb_line_core(hb, (const HiresClip *)0, x0, y0, x1, y1, set);
}

void hb_line(const HiresBitmap *hb, const HiresClip *clip, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set)
{
    _hb_line_core(hb, clip, x0, y0, x1, y1, set);
}

// -------------------------------------------------------------------------
// Mode switching -- see oric.h's "TEXT/HIRES mode-switch attributes" for the
// underlying hardware mechanism (last-column-byte trick, takes effect next
// scanline). Two facts drive this design, both confirmed by working through
// the ULA's TEXT/HIRES addressing formulas by hand:
//
//   1. hires_on()'s switch, once written at TEXTVRAM+39, is STICKY: every
//      frame, the ULA reads TEXT row 0 for its first raster line, hits the
//      switch attribute at column 39, and re-enters HIRES for the rest of
//      that same frame -- no per-frame re-poking needed. This costs row 0
//      of the TEXT screen exactly one raster line's worth of visibility
//      (a well-known Oric quirk: entering HIRES sacrifices a sliver of
//      TEXT row 0 every frame).
//   2. The HIRES buffer is EXACTLY 200 scanlines + a built-in 3-row TEXT
//      footer (24 more raster lines, since TEXT rows are 8 lines tall) =
//      224 total, matching the normal 28-row TEXT screen's own height
//      (28*8=224). So the ONLY clean (non-glitchy) place to switch back to
//      TEXT is the very LAST byte of the HIRES buffer (row 199's column
//      39, i.e. HIRESVRAM+HIRES_SIZE-1 == $BF3F) -- any earlier row would
//      land mid-character-row and glitch that row's first raster line.
//      This is also STICKY the same way: once $BF3F holds a TEXT-switch
//      attribute, every subsequent frame's rows 200-223 (the footer) show
//      TEXT content, while HIRES continues governing rows 0-199 above it
//      (since TEXTVRAM+39 hasn't changed) -- this is the normal way to get
//      a persistent HUD/status-bar footer under a HIRES picture.
//
// hires_off() is therefore NOT the same call as disabling the footer: it
// clears the STICKY trigger at TEXTVRAM+39 so HIRES stops being re-entered
// every frame, reverting fully to a normal 28-row TEXT screen from the
// following frame onward.
// -------------------------------------------------------------------------

void hires_on(bool pal50hz)
{
    uint8_t *p = (uint8_t *)(TEXTVRAM + SCREEN_COLS - 1);
    *p = pal50hz ? A_HIRES_50HZ : A_HIRES_60HZ;
}

void hires_off(void)
{
    // Clear the sticky HIRES-entry trigger; caller owns whatever else
    // belongs in that screen cell afterward (same convention as attribute
    // bytes elsewhere in this codebase consuming one screen column).
    uint8_t *p = (uint8_t *)(TEXTVRAM + SCREEN_COLS - 1);
    *p = CH_SPACE;
}

void hires_footer_enable(bool pal50hz)
{
    uint8_t *p = (uint8_t *)(HIRESVRAM + HIRES_SIZE - 1);
    *p = pal50hz ? A_TEXT_50HZ : A_TEXT_60HZ;
}

void hires_footer_disable(void)
{
    // Restore ordinary pixel data (bit6 set, all-paper) at the last HIRES
    // byte so the ULA renders it as bitmap data again instead of switching
    // to TEXT -- rows 200-223 then show whatever's left in unused memory
    // (undefined; callers that disable the footer shouldn't rely on that
    // area's contents).
    uint8_t *p = (uint8_t *)(HIRESVRAM + HIRES_SIZE - 1);
    *p = 0x40;
}

// -------------------------------------------------------------------------
// Attribute/colour API
//
// Unlike hb_set/clr/get/put (which take a pixel x coordinate, 0-239),
// these take a column-BYTE index (0-39) -- attributes are a byte-level
// concept, they don't have sub-byte pixel positions. Callers pass an
// A_FW*/A_BG* constant from oric.h directly (same convention as this
// codebase's existing cwin_put_attr()/row_setattr() in charwin.c -- no
// defensive masking, the attribute byte is written as given).
//
// hires_put_ink/put_paper/row_colors operate on the live screen
// (HIRESVRAM) only -- attributes only have meaning where the ULA is
// actually scanning them out, unlike pixel data which is meaningful on any
// off-screen HiresBitmap canvas too. hires_invert_byte() takes a
// HiresBitmap*, since the invert bit is just an ordinary bit of pixel data
// that survives a later bitblit from an off-screen canvas.
// -------------------------------------------------------------------------

void hires_put_ink(uint8_t col_byte, uint8_t y, uint8_t ink)
{
    *((uint8_t *)HIRESVRAM + hires_row_off[y] + col_byte) = ink;
}

void hires_put_paper(uint8_t col_byte, uint8_t y, uint8_t paper)
{
    *((uint8_t *)HIRESVRAM + hires_row_off[y] + col_byte) = paper;
}

// Sets INK at column-byte 0 and PAPER at column-byte 1 of row y -- same
// column order as charwin.c's row_setattr() (INK first, then PAPER).
void hires_row_colors(uint8_t y, uint8_t ink, uint8_t paper)
{
    hires_put_ink(0, y, ink);
    hires_put_paper(1, y, paper);
}

void hires_invert_byte(const HiresBitmap *hb, uint8_t xbyte, uint8_t y, bool on)
{
    uint8_t *p = hb->data + hires_row_off[y] + xbyte;
    if (on)
        *p |= 0x80;
    else
        *p &= (uint8_t)~0x80;
}

// -------------------------------------------------------------------------
// AIC (Alternate Inverted Colors) -- a demo-scene technique (popularised in
// games like Pulsoids) that fakes more perceived colours than the hardware
// truly has: a DIFFERENT ink/paper pair is used on even vs. odd scanlines,
// so eye/CRT blending of the two alternating lines produces intermediate
// apparent colours. ink[0]/paper[0] apply to even rows (y&1==0), ink[1]/
// paper[1] to odd rows.
// -------------------------------------------------------------------------

void hires_aic_init(HiresAIC *aic, uint8_t ink0, uint8_t paper0, uint8_t ink1, uint8_t paper1)
{
    aic->ink[0] = ink0;
    aic->paper[0] = paper0;
    aic->ink[1] = ink1;
    aic->paper[1] = paper1;
}

void hires_aic_apply_row(const HiresAIC *aic, uint8_t y)
{
    uint8_t parity = y & 1;
    hires_row_colors(y, aic->ink[parity], aic->paper[parity]);
}

void hires_aic_apply_range(const HiresAIC *aic, uint8_t y0, uint8_t y1)
{
    for (uint8_t y = y0; y <= y1; y++)
        hires_aic_apply_row(aic, y);
}

// -------------------------------------------------------------------------
// Rect fill, circle fill, triangle/polygon fill, bitblit, text
//
// These are all built on hb_get/hb_put (simple per-pixel loops), not
// hand-optimized byte-level bit-shifting like gfx/bitmap.c's heavily tuned
// 6502 routines -- correct and easy to verify first; a future pass could
// special-case byte-aligned/whole-byte spans for speed if a demo effect
// turns out to need it.
// -------------------------------------------------------------------------

void hb_rect_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool set)
{
    for (uint8_t row = 0; row < h; row++)
    {
        uint8_t py = (uint8_t)(y + row);
        if (clip && (py < clip->top || py > clip->bottom))
            continue;
        for (uint8_t col = 0; col < w; col++)
        {
            uint8_t px = (uint8_t)(x + col);
            if (!clip || (px >= clip->left && px <= clip->right))
                hb_put(hb, px, py, set);
        }
    }
}

// Filled circle: for each scanline within the circle's vertical extent,
// grow the half-width until it would exceed the radius, then fill that
// horizontal span. O(r) per row -- simple and robust at HIRES's small
// (240x200) resolution; no need for a Bresenham-style incremental circle.
void hb_circle_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t r, bool set)
{
    int16_t r2 = (int16_t)r * (int16_t)r;

    for (int16_t dy = -(int16_t)r; dy <= (int16_t)r; dy++)
    {
        int16_t py16 = (int16_t)cy + dy;
        if (py16 < 0 || py16 >= HIRES_ROWS)
            continue;
        uint8_t py = (uint8_t)py16;
        if (clip && (py < clip->top || py > clip->bottom))
            continue;

        int16_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2)
            dx++;

        for (int16_t x = -dx; x <= dx; x++)
        {
            int16_t px16 = (int16_t)cx + x;
            if (px16 < 0 || px16 >= HIRES_WIDTH_PX)
                continue;
            uint8_t px = (uint8_t)px16;
            if (!clip || (px >= clip->left && px <= clip->right))
                hb_put(hb, px, py, set);
        }
    }
}

// Even-odd (ray-casting) point-in-polygon test over a bounding box --
// handles convex and simple concave polygons alike with one algorithm,
// unlike gfx/bitmap.c's separate convex-only/non-convex routines.
static bool _hb_point_in_polygon(const uint8_t *xs, const uint8_t *ys, uint8_t num, int16_t px, int16_t py)
{
    bool inside = false;
    uint8_t j = (uint8_t)(num - 1);
    for (uint8_t i = 0; i < num; i++)
    {
        int16_t xi = xs[i], yi = ys[i], xj = xs[j], yj = ys[j];
        if ((yi > py) != (yj > py))
        {
            // Safe: yi != yj is guaranteed here, so no divide-by-zero.
            long xcross = (long)(xj - xi) * (py - yi) / (yj - yi) + xi;
            if (px < xcross)
                inside = !inside;
        }
        j = i;
    }
    return inside;
}

void hb_polygon_fill(const HiresBitmap *hb, const HiresClip *clip, const uint8_t *xs, const uint8_t *ys, uint8_t num, bool set)
{
    uint8_t minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    for (uint8_t i = 1; i < num; i++)
    {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }

    for (uint8_t py = miny; py <= maxy; py++)
    {
        if (clip && (py < clip->top || py > clip->bottom))
            continue;
        for (uint8_t px = minx; px <= maxx; px++)
        {
            if (clip && (px < clip->left || px > clip->right))
                continue;
            if (_hb_point_in_polygon(xs, ys, num, px, py))
                hb_put(hb, px, py, set);
        }
    }
}

void hb_triangle_fill(const HiresBitmap *hb, const HiresClip *clip,
                       uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, bool set)
{
    uint8_t xs[3] = { x0, x1, x2 };
    uint8_t ys[3] = { y0, y1, y2 };
    hb_polygon_fill(hb, clip, xs, ys, 3, set);
}

// -------------------------------------------------------------------------
// Bitblit -- copies (or combines) a w x h pixel region from a source
// canvas to a destination canvas. HiresBlitOp covers the common cases;
// unlike gfx/bitmap.c's BlitOp there is no pattern-fill variant here (Oric
// has no true per-pixel colour to dither a pattern into -- see hires.h).
// -------------------------------------------------------------------------

void hb_bitblit(const HiresBitmap *dst, const HiresClip *clip, uint8_t dx, uint8_t dy,
                 const HiresBitmap *src, uint8_t sx, uint8_t sy, uint8_t w, uint8_t h, HiresBlitOp op)
{
    for (uint8_t row = 0; row < h; row++)
    {
        uint8_t ty = (uint8_t)(dy + row);
        uint8_t fy = (uint8_t)(sy + row);
        if (clip && (ty < clip->top || ty > clip->bottom))
            continue;

        for (uint8_t col = 0; col < w; col++)
        {
            uint8_t tx = (uint8_t)(dx + col);
            uint8_t fx = (uint8_t)(sx + col);
            if (clip && (tx < clip->left || tx > clip->right))
                continue;

            bool s = hb_get(src, fx, fy);
            bool result;
            switch (op)
            {
                case HBLIT_OR:  result = s || hb_get(dst, tx, ty); break;
                case HBLIT_AND: result = s && hb_get(dst, tx, ty); break;
                case HBLIT_XOR: result = s != hb_get(dst, tx, ty); break;
                default:        result = s; break;  // HBLIT_COPY
            }
            hb_put(dst, tx, ty, result);
        }
    }
}

// -------------------------------------------------------------------------
// Text -- renders using the Oric ROM's standard 6x8 charset (CHARSETROM,
// see oric.h), printable ASCII 0x20-0x7F only. A ROM glyph byte's bits5-0
// are already left-aligned exactly as HIRES pixel bytes expect (bit5 =
// leftmost) -- see oric.h's CHARSETROM documentation.
// -------------------------------------------------------------------------

int hb_put_chars(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, const char *str, uint8_t len)
{
    uint8_t drawn = 0;
    for (uint8_t i = 0; i < len && str[i]; i++)
    {
        uint8_t ch = (uint8_t)str[i];
        if (ch < 0x20)
            break;

        const uint8_t *glyph = (const uint8_t *)(CHARSETROM + (uint16_t)(ch - 0x20) * 8);
        for (uint8_t row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            uint8_t py = (uint8_t)(y + row);
            if (clip && (py < clip->top || py > clip->bottom))
                continue;
            for (uint8_t col = 0; col < 6; col++)
            {
                uint8_t px = (uint8_t)(x + drawn * 6 + col);
                if (clip && (px < clip->left || px > clip->right))
                    continue;
                hb_put(hb, px, py, (bits & (0x20 >> col)) != 0);
            }
        }
        drawn++;
    }
    return drawn * 6;
}
