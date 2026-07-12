// section_hires_showcase.c - see section_hires_showcase.h.
//
// Four hires.h fill primitives, each appearing in its own turn, each
// confined to its own non-overlapping horizontal band so it can have its
// own plain ink colour via hires_row_colors() with no risk of an
// ink-bracket collision (every band's shape never shares a row with any
// other band's shape):
//   - y  5- 50 (green):   hb_ellipse_fill()
//   - y 52-102 (cyan):    hb_polygon_fill() -- a 10-vertex star
//   - y104-144 (red):     hb_rect_pattern() -- a diagonal hatch tile
//   - y146-196 (magenta): hb_circle_fill() x2 (a ring: solid disc, then a
//     smaller disc punched out of its centre) followed by hb_flood_fill()
//     re-filling that punched-out centre -- demonstrates flood_fill()
//     specifically (paint-bucket filling a bounded blank region), not
//     just another filled shape.
//
// Also lands the demo's music-track switch: assets/steppingout.aky (used
// by sections 1-3) hands off to assets/boulesetbits.aky here, the first
// section past the split point (see main.c's own "Two music tracks"
// comment for the bracketing rationale -- arkos_stop()/arkos_load()/
// arkos_init(), NOT arkos_pause()/arkos_resume(), since restarting from
// the beginning is exactly the intent for a genuine track change, unlike
// section_logo.c's picture_load() which needs the CURRENT track to keep
// playing through an unrelated asset load).

#include "oric.h"
#include "hires.h"
#include "arkos.h"
#include "rasterirq.h"
#include "section_hires_showcase.h"

#ifdef STORAGE_FLOPPY
#define MUSIC_FILE2 3
#else
#define MUSIC_FILE2 "boulesetbits.aky"
#endif

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
    // The music-track switch -- see this file's own header comment for
    // why this pair (arkos_stop()/arkos_load()/arkos_init()), not
    // arkos_pause()/arkos_resume(), is the right one here. Bracketed with
    // hrirq_stop()/hrirq_start() for the same reason picture_load() is:
    // neither file_load() nor floppy_load() is safe to call while
    // arkos_tick() is ticking live (see docs/arkos.md's "Pause vs. stop").
    hrirq_stop();
    arkos_stop();
    if (arkos_load(MUSIC_FILE2))
        arkos_init();
    hrirq_start();

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
        uint8_t y;
        hb_polygon_fill(screen, (const HiresClip *)0, star_xs, star_ys, 10, true);
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
