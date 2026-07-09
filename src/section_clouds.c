// section_clouds.c - parallax cloud layer, upper sky band (HIRES mode)
//
// A sub-canvas over the sky's upper rows (see CLOUD_TOP/CLOUD_ROWS below),
// scrolled independently of everything else via hires.h's
// hb_scroll_left_fast -- a real, visible parallax layer once combined with
// section_bird.c's own horizontal movement (bird moves every tick, clouds
// scroll every CLOUD_SCROLL_EVERY ticks: two independent speeds/depths).
//
// Deliberately kept within rows 14-33: confirmed clear of the row range
// that triggers the footer-interaction rendering bug documented in project
// memory (project_phosphoric_footer_color_bug) -- that bug only reproduced
// with pixel content near the creek/footer end of the screen (row ~170+),
// not here. Don't move this band down near the creek without re-testing.
//
// ALSO deliberately kept clear of section_bird.c's own row range (bird's
// sprite reaches as high as row 35 at the top of its sine wave -- see that
// file's BIRD_BASE_Y/BIRD_AMPLITUDE comment) with a 1-row margin. This
// matters for a reason beyond simple visual overlap: this layer's own
// scroll can run BETWEEN a bird XOR draw and its later XOR erase (main.c's
// loop calls section_clouds_tick() then section_bird_tick() every
// iteration, and bird_tick() only re-erases the PREVIOUS frame's position
// on its NEXT call) -- if a cloud-scroll shifted bytes under a
// currently-drawn bird, the bird's later erase XORs its shape onto
// ALREADY-CHANGED data instead of the exact bytes it drew onto, leaving
// permanent stray pixel garbage (this exact bug was reported as "yellow
// corruption below clouds" running in real Oricutron -- not reproduced in
// this project's own Phosphoric testing, which never happened to catch
// the bird at a row low enough to overlap the old, taller cloud band).
// Keeping the two sections' row ranges disjoint sidesteps the whole
// problem rather than trying to synchronise scroll timing against
// whatever sprite happens to be mid-draw.
//
// Clouds are simple 3-ellipse silhouettes (hb_ellipse_fill), ink-coloured
// (white, matching the whole screen's fixed ink baseline -- see
// section_background.c) against the sky's cyan paper. Scrolling vacates
// column-bytes with 0x40 (blank pixel data, matching the sky's own empty
// look) at the trailing (right) edge; a fresh cloud is drawn there
// periodically so the band never runs empty over a long-running demo.
//
// Column-bytes 0-1 of EVERY row are permanently reserved for that row's
// own ink/paper attribute bytes (see section_background.c/section_bird.c's
// own comments on this) -- ordinary hb_scroll_left_fast shifts a row's
// FULL 40 bytes uniformly, which would eventually drag cloud pixel content
// into columns 0-1 as it drifts left over many scroll steps, silently and
// permanently converting that row's colour attribute into stray pixel
// data (a real bug caught the same way as section_bird.c's BIRD_MIN_COL
// one: confirmed via RAM dump, not just a screenshot glance, when the
// first cloud's own leftmost ellipse edge briefly overlapped column 1
// before this fix). clouds_scroll_left() below scrolls only bytes 2-39,
// leaving 0-1 untouched forever, so cloud content simply ages out at
// column 2 rather than ever reaching the reserved columns.

#include <string.h>
#include "oric.h"
#include "hires.h"
#include "section_clouds.h"

#define CLOUD_TOP           14u
#define CLOUD_ROWS          20u
#define CLOUD_SCROLL_EVERY  6u    // main-loop ticks between each 6px scroll step
#define CLOUD_SPAWN_EVERY   10u   // scroll steps between a fresh cloud spawning at the right edge
#define CLOUD_MIN_X         25u   // keeps the widest ellipse's leftmost edge (x-10) at column-byte >=2

static HiresBitmap cloud_canvas;
static uint8_t tick_count = 0;
static uint8_t scroll_count = 0;

static void draw_cloud(uint8_t x, uint8_t y)
{
    hb_ellipse_fill(&cloud_canvas, (const HiresClip *)0, x,      y,     10, 5, true);
    hb_ellipse_fill(&cloud_canvas, (const HiresClip *)0, x + 8,  y - 3, 8,  4, true);
    hb_ellipse_fill(&cloud_canvas, (const HiresClip *)0, x + 16, y,     9,  5, true);
}

static void clouds_scroll_left(void)
{
    for (uint8_t y = 0; y < CLOUD_ROWS; y++)
    {
        uint8_t *row = cloud_canvas.data + (uint16_t)y * HIRES_ROW_BYTES;
        memmove(row + 2, row + 3, HIRES_ROW_BYTES - 3);
        row[HIRES_ROW_BYTES - 1] = 0x40;
    }
}

void section_clouds_init(const HiresBitmap *screen)
{
    hb_init(&cloud_canvas, screen->data + (uint16_t)CLOUD_TOP * HIRES_ROW_BYTES, CLOUD_ROWS);

    draw_cloud(CLOUD_MIN_X, 10);
    draw_cloud(110, 13);
    draw_cloud(190, 8);
}

void section_clouds_tick(const HiresBitmap *screen)
{
    (void)screen;

    tick_count++;
    if (tick_count < CLOUD_SCROLL_EVERY)
        return;
    tick_count = 0;

    clouds_scroll_left();

    scroll_count++;
    if (scroll_count >= CLOUD_SPAWN_EVERY)
    {
        scroll_count = 0;
        draw_cloud(HIRES_WIDTH_PX - 40, 12);
    }
}
