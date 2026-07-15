// scroller.c - see scroller.h.

#include <string.h>
#include "scroller.h"
#include "fixedmath.h"
#include "oric.h"

// Column-bytes advanced per scroller_tick() call -- 6px, one whole
// character. See scroller.h's own header comment for why this replaced
// the old smooth-but-slow per-pixel design.
#define SCROLL_STEP_COLS    1

// Ticks (main-loop iterations, ~60ms each) to wait between advancing
// text_start_col -- NOT a smaller/fractional SCROLL_STEP_COLS: a genuine
// sub-byte (2-3px) scroll would need every visible column's displayed
// byte re-derived as a bit-shifted composite of TWO adjacent glyph bytes
// every step, real new complexity and per-tick cost that reintroduces
// much of what the byte-aligned redesign above was specifically built to
// eliminate. A plain tick delay (same technique as
// section_dissolve_showcase's own DISSOLVE_TICKS_PER_STEP, before that
// section was removed) gets the same practical "easier to read" result
// -- slower motion -- at zero risk, keeping the whole byte-aligned copy
// unchanged. 2 ticks/step here roughly halves the scroll speed (was
// reported "too fast" at 1 tick/step); raise further if still too fast.
#define SCROLLER_TICKS_PER_STEP 2u

#define BOUNCE_AMPLITUDE    6   // max +/- vertical offset in pixels (SCROLLER_BOUNCE)
// Deliberately NOT "6u": mixing an unsigned literal into an expression
// with oric_cos()'s SIGNED result silently promotes the whole expression
// to unsigned, breaking negative-value handling -- the exact bug this
// project has hit (and fixed) twice already, in
// section_wave_showcase.c's MAX_AMPLITUDE and this file's own earlier
// BOUNCE_AMPLITUDE. Plain signed `6` keeps this computation in signed
// arithmetic.
#define BOUNCE_ANGLE_STEP   4u  // oric_cos() angle units per tick -- controls bounce speed

static const char    *text;
static uint8_t        text_len;   // strlen(text), capped to uint8_t
static ScrollerStyle  style;
static uint8_t        base_y;
static int16_t        text_start_col;  // column-byte where text[0] currently sits (may be off-screen)
static uint8_t         bounce_angle;
static uint8_t         prev_y;
static bool            has_prev;
static uint8_t         step_tick_count;

static uint8_t max_col(void)
{
    return (uint8_t)(HIRES_ROW_BYTES - 1u);
}

// Blanks one column-byte band (SCROLLER_GLYPH_H rows tall, the full
// SCROLLER_MIN_COL..max_col() width) -- used to clear the OLD row band
// when SCROLLER_BOUNCE moves y between ticks (a plain-style scroll never
// needs this, since its y never changes and every tick's own full-width
// redraw already overwrites the same rows).
static void blank_band(const HiresBitmap *screen, uint8_t y)
{
    uint8_t row;
    for (row = 0; row < SCROLLER_GLYPH_H; row++)
    {
        uint8_t *p = screen->data + (uint16_t)(y + row) * HIRES_ROW_BYTES + SCROLLER_MIN_COL;
        memset(p, 0x40, max_col() - SCROLLER_MIN_COL + 1u);
    }
}

void scroller_init(const HiresBitmap *screen, const char *s, uint8_t y, ScrollerStyle st)
{
    (void)screen;
    text           = s;
    text_len       = (uint8_t)strlen(s);
    style          = st;
    base_y         = y;
    text_start_col = (int16_t)max_col() + 1;   // starts just off the right edge
    bounce_angle   = 0;
    has_prev       = false;
    step_tick_count = 0;
}

bool scroller_tick(const HiresBitmap *screen)
{
    uint8_t y_base;
    uint8_t c;

    if (style == SCROLLER_BOUNCE)
    {
        bounce_angle = (uint8_t)(bounce_angle + BOUNCE_ANGLE_STEP);
        y_base = (uint8_t)(base_y + ((int16_t)oric_cos(bounce_angle) * BOUNCE_AMPLITUDE) / 127);
    }
    else
    {
        y_base = base_y;
    }

    // SCROLLER_BOUNCE moved since last tick -- blank the OLD band first,
    // since this tick's own full-width redraw only touches the NEW y.
    if (has_prev && prev_y != y_base)
        blank_band(screen, prev_y);

    for (c = SCROLLER_MIN_COL; c <= max_col(); c++)
    {
        int16_t str_col = (int16_t)c - text_start_col;
        const uint8_t *glyph = (str_col >= 0 && str_col < (int16_t)text_len)
                                    ? (const uint8_t *)(HIRES_CHARSET_STD + (uint16_t)((uint8_t)text[str_col]) * 8)
                                    : (const uint8_t *)0;
        uint8_t row;

        for (row = 0; row < SCROLLER_GLYPH_H; row++)
        {
            uint8_t *p = screen->data + (uint16_t)(y_base + row) * HIRES_ROW_BYTES + c;
            *p = glyph ? (uint8_t)(glyph[row] | 0x40) : 0x40;
        }
    }

    prev_y   = y_base;
    has_prev = true;

    if ((int16_t)text_start_col + (int16_t)text_len < (int16_t)SCROLLER_MIN_COL)
        return true;   // fully scrolled off the left edge

    // Only the COLUMN advance is throttled -- the redraw above (and
    // SCROLLER_BOUNCE's own vertical wobble) still runs every tick, so
    // bounce motion stays smooth even though horizontal movement is
    // slower. See SCROLLER_TICKS_PER_STEP's own comment for why this is
    // a delay, not a smaller SCROLL_STEP_COLS.
    step_tick_count++;
    if (step_tick_count < SCROLLER_TICKS_PER_STEP)
        return false;
    step_tick_count = 0;

    text_start_col = (int16_t)(text_start_col - SCROLL_STEP_COLS);

    return false;
}
