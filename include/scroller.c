// scroller.c - see scroller.h.

#include <string.h>
#include "scroller.h"
#include "fixedmath.h"
#include "oric.h"

#define SCROLL_STEP        2u   // pixels per scroller_tick() call
// Deliberately NOT "6u": a real, pre-existing latent bug (found via a
// soak-test hang once a SECOND scroller-using section existed to expose
// it) came from mixing this constant, unsigned, into `new_y`'s own
// computation below with oric_cos()'s SIGNED result -- C's usual
// arithmetic conversions silently promote the whole expression to
// unsigned when a signed and unsigned operand of matching rank are
// mixed, breaking negative-value handling (a negative cos() value's bit
// pattern gets reinterpreted as a huge positive number instead of
// bringing `new_y` back within a small +/- offset of base_y) -- see
// src/section_wave_showcase.c's own header comment for the twin bug this
// was first found and fixed in. Plain signed `6` keeps that whole
// computation in signed arithmetic.
#define BOUNCE_AMPLITUDE    6   // max +/- vertical offset in pixels (SCROLLER_BOUNCE)
#define BOUNCE_ANGLE_STEP   4u  // oric_cos() angle units per tick -- controls bounce speed

static const char    *text;
static uint8_t        text_len;   // strlen(text), capped to uint8_t
static ScrollerStyle  style;
static uint8_t        base_y;
static uint16_t       text_width;
static uint16_t       distance;        // total non-wrapping pixels scrolled so far
static uint16_t       total_distance;  // scroll completes once distance reaches this
static uint8_t        prev_x, prev_y;  // position of the LAST draw, for erasing
static uint8_t        bounce_angle;
static bool           has_prev;

void scroller_init(const HiresBitmap *screen, const char *s, uint8_t y, ScrollerStyle st)
{
    (void)screen;
    text           = s;
    text_len       = (uint8_t)strlen(s);
    style          = st;
    base_y         = y;
    text_width     = (uint16_t)text_len * SCROLLER_GLYPH_W;
    distance       = 0;
    total_distance = (uint16_t)(HIRES_WIDTH_PX + text_width);
    bounce_angle   = 0;
    has_prev       = false;
}

bool scroller_tick(const HiresBitmap *screen)
{
    HiresClip clip;
    uint8_t new_x, new_y;

    clip.left   = SCROLLER_MIN_X;
    clip.right  = HIRES_WIDTH_PX - 1;
    clip.top    = 0;
    clip.bottom = HIRES_ROWS - 1;

    // Erase the previous frame's bounding box first -- text_width is
    // capped to 255 here (hb_rect_fill's own w parameter is uint8_t);
    // taglines/credits lines are expected to stay well under that.
    if (has_prev)
        hb_rect_fill(screen, &clip, prev_x, prev_y, (uint8_t)text_width, SCROLLER_GLYPH_H, false);

    if (distance >= total_distance)
        return true;   // fully scrolled off the left edge

    new_x = (uint8_t)((int16_t)HIRES_WIDTH_PX - (int16_t)distance);

    if (style == SCROLLER_BOUNCE)
    {
        bounce_angle = (uint8_t)(bounce_angle + BOUNCE_ANGLE_STEP);
        new_y = (uint8_t)(base_y + ((int16_t)oric_cos(bounce_angle) * BOUNCE_AMPLITUDE) / 127);
    }
    else
    {
        new_y = base_y;
    }

    hb_put_chars(screen, &clip, new_x, new_y, text, text_len);

    prev_x   = new_x;
    prev_y   = new_y;
    has_prev = true;

    distance = (uint16_t)(distance + SCROLL_STEP);

    return false;
}
