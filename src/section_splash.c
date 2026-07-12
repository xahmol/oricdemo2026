// section_splash.c - see section_splash.h.
//
// HIRES-mode section (see git history for why it moved from TEXT mode --
// short version: the floppy target's own boot sector left the video
// hardware's ALT-charset addressing pointed somewhere this section's own
// writes couldn't reach, a bug HIRES bitmap rendering sidesteps entirely
// since it doesn't depend on that mechanism at all).
//
// The "idi8b" wordmark is drawn by walking idi8b_logo[]'s own original
// TEXT-mode-layout bytes (rows 0-11 only -- row 12, the plain-text
// "IDreamtIn8Bits.com" line, is drawn separately, see below) and, for
// each character cell, looking up its glyph bitmap in HIRES_CHARSET_ALT
// (populated once at boot from assets/idi8b_altcharset.h -- see main.c)
// exactly the way a real Oric ALT-charset consumer would, just rendering
// the result as HIRES pixels via hb_put() instead of through the TEXT-mode
// hardware. This reproduces the ORIGINAL mosaic texture (idi8b_logo's own
// dithered/striped look) faithfully -- an earlier version of this file
// drew 5 plain solid-coloured rectangles instead, which was much simpler
// but didn't actually look like the source logo.
//
// Ink brackets (hires_put_ink) sit ONE COLUMN BEFORE each letter's own
// first content column (5/8/15/18/26, not 6/9/16/19/27) -- putting a
// bracket AT a content column collided with the dissolve's own hb_put()
// writes to that exact byte, clobbering the ink attribute back into
// stray pixel data (confirmed via screenshot: all 5 letters rendered
// white/noisy instead of coloured). Same hazard section_bird.c's
// HxsprColor bracket already established the fix for.

#include "oric.h"
#include "hires.h"
#include "ttf.h"
#include "handwriting_font.h"
#include "idi8b_logo.h"
#include "section_splash.h"
#include "section_common.h"

#define SPLASH_LOGO_ROWS  12u    // idi8b_logo[] rows 0-11 (row 12 is separate)
#define SPLASH_LOGO_COLS  40u
#define SPLASH_CELL_COUNT (SPLASH_LOGO_ROWS * SPLASH_LOGO_COLS)   // 480

// Vertical layout: logo (12 rows * 8px = 96px) + gap + "IDreamtIn8Bits.com"
// (8px, standard charset) + gap + "presents...." (26px, handwriting
// script) -- total 146px, centred in the 200px-tall screen (27px margin
// top/bottom).
#define SPLASH_TOP_Y        27u
#define SPLASH_LOGO_BOTTOM  (SPLASH_TOP_Y + SPLASH_LOGO_ROWS * 8u - 1u)   // 122
#define TEXT_LINE_Y        131u   // "IDreamtIn8Bits.com" -- standard charset (hb_put_chars)
#define SCRIPT_LINE_Y      147u   // "presents...." -- handwriting_font (TTF), per the user's request
#define SPLASH_ERASE_TOP    SPLASH_TOP_Y
#define SPLASH_ERASE_BOTTOM 178u

// Cells revealed/hidden per tick -- 480/40 = 12 ticks per fade
// (~12 * ~74ms main-loop iteration =~ 0.9s): most of the 480 positions are
// blank and only cost a byte comparison, not a draw (see splash_step()),
// so this is fast despite iterating the full row*col space rather than
// just the ~170 actually-on cells -- simpler code (no separate "on cell"
// list to build), and still comfortably fast enough. An earlier
// per-PIXEL dissolve (12960 positions) was reported as "too slow"; this
// per-CELL version reveals a whole 6x8 glyph per draw instead of one
// pixel, cutting the total unit count by ~27x.
#define SPLASH_CELLS_PER_TICK 40u

// How many ticks to hold the fully-revealed splash before fading back out
// (~60 * ~74ms =~ 4.4s -- long enough to read, not so long it drags; cut
// down from an earlier, slower version per user feedback on overall pace).
#define SPLASH_HOLD_TICKS 60u

typedef enum
{
    SPLASH_FADE_IN,
    SPLASH_HOLD,
    SPLASH_FADE_OUT,
    SPLASH_DONE
} SplashState;

static SplashState splash_state;
static uint16_t    splash_draws_done;
static uint16_t    splash_hold_count;

// 16-bit Galois LFSR (polynomial x^16+x^14+x^13+x^11+1, taps 0xB400) --
// same technique as dissolve.h's own hires_dissolve_next(), reimplemented
// here (not called directly) since that function's range is hardcoded to
// the full 240x200 HIRES pixel count, not this section's 480 TEXT-layout
// cells. Full period 65535 visits every nonzero 16-bit state exactly once
// and never reaches 0 -- rejecting anything OUTSIDE [1, SPLASH_CELL_COUNT]
// and returning val-1 keeps cell 0 (row 0, col 0 -- blank in this data,
// so it wouldn't matter here either way, but kept consistent with the
// same reasoning used elsewhere in this project) reachable.
static uint16_t splash_lfsr;

static void splash_lfsr_init(uint16_t seed)
{
    splash_lfsr = seed ? seed : 1;
}

static uint16_t splash_lfsr_next(void)
{
    uint16_t val;
    do
    {
        uint16_t lsb = (uint16_t)(splash_lfsr & 1);
        splash_lfsr = (uint16_t)(splash_lfsr >> 1);
        if (lsb)
            splash_lfsr = (uint16_t)(splash_lfsr ^ 0xB400);
        val = splash_lfsr;
    } while (val > SPLASH_CELL_COUNT);
    return (uint16_t)(val - 1);
}

// Draws (set=true, using the glyph's own bit pattern) or erases (set=
// false, forcing all 48 pixels blank regardless of the glyph) one 6x8
// character cell at TEXT-layout column `col`, logo row `row` (0-11).
static void splash_draw_cell(const HiresBitmap *screen, uint8_t row, uint8_t col, uint8_t code, bool set)
{
    const uint8_t *glyph = (const uint8_t *)(HIRES_CHARSET_ALT + (uint16_t)code * 8);
    uint8_t x0 = (uint8_t)(col * 6);
    uint8_t y0 = (uint8_t)(SPLASH_TOP_Y + row * 8);
    uint8_t py;
    for (py = 0; py < 8; py++)
    {
        uint8_t bits = glyph[py];
        uint8_t px;
        for (px = 0; px < 6; px++)
            hb_put(screen, (uint8_t)(x0 + px), (uint8_t)(y0 + py), set && (bits & (0x20 >> px)) != 0);
    }
}

// Reveals/hides up to SPLASH_CELLS_PER_TICK cells (row-major positions
// into idi8b_logo[]'s own rows 0-11), skipping any whose byte isn't one
// of the 3 mosaic glyph codes (blank cells never need touching either
// way, since they're already blank and stay that way).
static void splash_step(const HiresBitmap *screen, bool set)
{
    uint16_t i;
    for (i = 0; i < SPLASH_CELLS_PER_TICK && splash_draws_done < SPLASH_CELL_COUNT; i++)
    {
        uint16_t pos = splash_lfsr_next();
        uint8_t code = idi8b_logo[pos];
        splash_draws_done++;
        if (code == 0x21 || code == 0x30 || code == 0x35)
        {
            uint8_t row = (uint8_t)(pos / SPLASH_LOGO_COLS);
            uint8_t col = (uint8_t)(pos % SPLASH_LOGO_COLS);
            splash_draw_cell(screen, row, col, code, set);
        }
    }
}

void section_splash_init(const HiresBitmap *screen)
{
    uint8_t y;

    hb_fill(screen, 0x40);   // real RAM isn't zero-initialized -- start blank

    // Baseline ink/paper for the whole screen (white ink, black paper) --
    // establishes a known, predictable state for every row before the
    // per-letter ink overrides below, same convention as
    // section_background.c's own baseline sweep.
    for (y = 0; y < HIRES_ROWS; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);

    // Per-letter ink overrides, one hires_put_ink() per bracket column per
    // row spanned -- cascades rightward until the next bracket overrides
    // it again (see hires.h's own ink-cascade convention). Column numbers
    // read directly off idi8b_logo[]'s own structure (every one of rows
    // 0-11 uses the same 5 letter-slot column ranges: i1 6-7, d 9-14,
    // i2 16-17, 8 19-25, b 27-32), bracket = first content column - 1.
    for (y = SPLASH_TOP_Y; y <= SPLASH_LOGO_BOTTOM; y++)
    {
        hires_put_ink(5, y, A_FWGREEN);     // i1 (cols 6-7)
        hires_put_ink(8, y, A_FWCYAN);      // d  (cols 9-14)
        hires_put_ink(15, y, A_FWBLUE);     // i2 (cols 16-17)
        hires_put_ink(18, y, A_FWRED);      // 8  (cols 19-25)
        hires_put_ink(26, y, A_FWMAGENTA);  // b  (cols 27-32)
    }

    splash_lfsr_init(1);
    splash_state = SPLASH_FADE_IN;
    splash_draws_done = 0;
}

void section_splash_tick(const HiresBitmap *screen)
{
    switch (splash_state)
    {
    case SPLASH_FADE_IN:
        splash_step(screen, true);
        if (splash_draws_done >= SPLASH_CELL_COUNT)
        {
            hb_put_chars_center(screen, (const HiresClip *)0, TEXT_LINE_Y, "IDreamtIn8Bits.com", 18);
            ttf_print_center(screen, (const HiresClip *)0, &handwriting_font, SCRIPT_LINE_Y, "presents....");
            splash_state = SPLASH_HOLD;
            splash_hold_count = 0;
        }
        break;

    case SPLASH_HOLD:
        splash_hold_count++;
        if (splash_hold_count >= SPLASH_HOLD_TICKS)
        {
            hb_rect_fill(screen, (const HiresClip *)0, 0, SPLASH_ERASE_TOP,
                         HIRES_WIDTH_PX, SPLASH_ERASE_BOTTOM - SPLASH_ERASE_TOP + 1, false);
            splash_state = SPLASH_FADE_OUT;
            splash_draws_done = 0;
            // Deliberately NOT re-seeding the LFSR: continuing the same
            // running state naturally yields a different sub-sequence of
            // cell positions than the fade-in did (the underlying
            // 65535-long period is far longer than the ~960 draws two
            // full fades consume, so no meaningful repeat risk).
        }
        break;

    case SPLASH_FADE_OUT:
        splash_step(screen, false);
        if (splash_draws_done >= SPLASH_CELL_COUNT)
        {
            splash_state = SPLASH_DONE;
            section_mark_finished();
        }
        break;

    default:
        section_mark_finished();
        break;
    }
}
