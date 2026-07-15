// section_hires_showcase.c - see section_hires_showcase.h.
//
// Four hires.h fill primitives, each appearing in its own turn, each
// confined to its own non-overlapping horizontal band so it can have its
// own plain ink colour via hires_row_colors() with no risk of an
// ink-bracket collision (every band's shape never shares a row with any
// other band's shape):
//   - y  5- 50 (green):   hb_ellipse_fill()
//   - y 52-102 (cyan):    a 10-vertex star, hb_line() outline + a single
//     hb_flood_fill() from its own centroid (120,77) -- NOT
//     hb_polygon_fill() (tried first; see below), demonstrating
//     flood_fill() a second time here alongside the ring's own use below.
//   - y104-144 (red):     hb_rect_pattern() -- a diagonal hatch tile
//   - y146-196 (magenta): hb_circle_fill() x2 (a ring: solid disc, then a
//     smaller disc punched out of its centre) followed by hb_flood_fill()
//     re-filling that punched-out centre -- demonstrates flood_fill()
//     specifically (paint-bucket filling a bounded blank region), not
//     just another filled shape.
//
// The star used hb_polygon_fill() originally, matching Oscar64's own
// hires sample this section is loosely modelled on. Switched away from it
// (2026-07-15, alongside a similar fix in src/section_rasterirq_showcase.c)
// after discovering hb_polygon_fill()'s own per-pixel x per-edge
// point-in-polygon test (hires.c's _hb_point_in_polygon(), which runs a
// DIVISION per edge per pixel) takes several real SECONDS to fill a shape
// this size on a 1MHz 6502 -- confirmed via Phosphoric frame-dumps showing
// the star visibly growing over that whole time, not an instant fill, at
// this exact call site (PHASE_WAIT_TICKS=15, ~1.1s, is nowhere near long
// enough to cover it -- the fill call itself blocks for far longer than
// that before the state machine even gets to advance). hb_polygon_fill()
// SEPARATELY also has a real, documented, unresolved Oscar64 -O2
// whole-program register-allocator bug at SOME call sites (silently drops
// most of its fill loop's iterations -- see ~/.claude/oscar64.md's "Third
// symptom shape" entry, 2026-07-12) that resisted every previously-tried
// fix; this project's own established response to that (see
// section_polygon_workout.c's/section_background.c's own header comments)
// is to route around the function entirely rather than keep debugging it
// -- the same call here. hb_line()+hb_flood_fill() has neither problem:
// flood_fill is a scanline-stack algorithm with no per-pixel division (see
// hires.c's own header comment on it), and it was already linked into
// this exact file for the ring effect below.
//
// Used to hand off the demo's music track here (a ONE-TIME hardcoded
// switch to assets/boulesetbits.aky, the first section past the original
// "two tracks split at section 4" design). Superseded by a general,
// completion-driven toggle in main.c (music_check_toggle(), which
// alternates tracks whenever the currently playing one finishes a full
// playthrough via arkos_song_finished() -- see arkos.c's own "end of
// song" detection) that now runs for the WHOLE demo, not just from this
// section onward -- per user feedback asking for exactly that behaviour.
// This section no longer touches music at all.

#include "oric.h"
#include "hires.h"
#include "section_hires_showcase.h"

// Ticks to hold each shape before the next one appears (~15 * ~74ms
// =~ 1.1s -- long enough to register each shape individually, not so
// long the whole showcase drags).
#define PHASE_WAIT_TICKS 15u

#define ELLIPSE_CX  120u
#define ELLIPSE_CY   27u
#define ELLIPSE_RX   90u
#define ELLIPSE_RY   20u

#define PATTERN_X    20u
#define PATTERN_Y   110u
#define PATTERN_W   200u
#define PATTERN_H    30u

#define RING_CX     120u
#define RING_CY     171u
#define RING_R_OUT   24u
#define RING_R_IN    17u

static const uint8_t star_xs[10] = { 120, 129, 153, 134, 141, 120, 99, 106, 87, 111 };
static const uint8_t star_ys[10] = {  52,  69,  69,  80,  97,  88, 97,  80, 69,  69 };
// Centroid of the 10 vertices above -- a valid hb_flood_fill() seed for
// this specific (star-shaped, symmetric) polygon; see this file's own
// header comment for why this replaced hb_polygon_fill().
#define STAR_CENTER_X 120u
#define STAR_CENTER_Y  77u

static const uint8_t hatch_pattern[8] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};

typedef enum
{
    SHOW_ELLIPSE,
    SHOW_STAR,
    SHOW_PATTERN,
    SHOW_RING,
    SHOW_FLOOD,
    SHOW_DONE
} ShowState;

static ShowState state;
static uint16_t  wait_count;

void section_hires_showcase_init(const HiresBitmap *screen)
{
    hb_fill(screen, 0x40);   // real RAM isn't zero-initialized -- start blank

    hires_row_colors(0, A_FWWHITE, A_BGBLACK);   // whole-screen baseline
    {
        uint8_t y;
        for (y = 1; y < HIRES_ROWS; y++)
            hires_row_colors(y, A_FWWHITE, A_BGBLACK);
    }

    state = SHOW_ELLIPSE;
    wait_count = 0;
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): holds the completed picture
// forever once SHOW_DONE is reached, paced externally by main.c's own
// HIRES_SHOWCASE_MAX_TICKS.
void section_hires_showcase_tick(const HiresBitmap *screen)
{
    wait_count++;
    if (wait_count < PHASE_WAIT_TICKS)
        return;
    wait_count = 0;

    switch (state)
    {
    case SHOW_ELLIPSE:
    {
        uint8_t y;
        hb_ellipse_fill(screen, (const HiresClip *)0, ELLIPSE_CX, ELLIPSE_CY, ELLIPSE_RX, ELLIPSE_RY, true);
        for (y = 5; y <= 50; y++)
            hires_row_colors(y, A_FWGREEN, A_BGBLACK);
        state = SHOW_STAR;
        break;
    }

    case SHOW_STAR:
    {
        uint8_t y, i;
        for (i = 0; i < 10; i++)
        {
            uint8_t j = (uint8_t)((i + 1u) % 10u);
            hb_line(screen, (const HiresClip *)0, star_xs[i], star_ys[i], star_xs[j], star_ys[j], true);
        }
        hb_flood_fill(screen, (const HiresClip *)0, STAR_CENTER_X, STAR_CENTER_Y, true);
        for (y = 52; y <= 102; y++)
            hires_row_colors(y, A_FWCYAN, A_BGBLACK);
        state = SHOW_PATTERN;
        break;
    }

    case SHOW_PATTERN:
    {
        uint8_t y;
        hb_rect_pattern(screen, (const HiresClip *)0, PATTERN_X, PATTERN_Y, PATTERN_W, PATTERN_H, hatch_pattern);
        for (y = 104; y <= 144; y++)
            hires_row_colors(y, A_FWRED, A_BGBLACK);
        state = SHOW_RING;
        break;
    }

    case SHOW_RING:
    {
        uint8_t y;
        hb_circle_fill(screen, (const HiresClip *)0, RING_CX, RING_CY, RING_R_OUT, true);
        hb_circle_fill(screen, (const HiresClip *)0, RING_CX, RING_CY, RING_R_IN, false);
        for (y = 146; y <= 196; y++)
            hires_row_colors(y, A_FWMAGENTA, A_BGBLACK);
        state = SHOW_FLOOD;
        break;
    }

    case SHOW_FLOOD:
        // Paint-bucket: the ring's own punched-out centre is still blank
        // (the SHOW_RING phase's second hb_circle_fill() call, set=false)
        // -- flood_fill from the centre point fills exactly that bounded
        // blank region back in, demonstrating flood_fill() specifically
        // rather than just drawing another filled shape.
        hb_flood_fill(screen, (const HiresClip *)0, RING_CX, RING_CY, true);
        state = SHOW_DONE;
        break;

    default:
        break;   // holds the completed picture -- paced externally
    }
}
