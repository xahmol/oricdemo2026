// section_background.c - sky, green bank + trees, and an animated river (HIRES mode)
//
// Three plain PAPER colour bands (sky, bank, river) with pixel content
// drawn on top: a few simple silhouette trees on the bank, and a wavy
// ink line + horizontal-stripe shimmer texture in the river, scrolled a
// little each tick (section_background_tick()) to suggest flowing water.
// Ink stays white everywhere (matching main.c's whole-screen baseline) --
// only PAPER varies by band, so section_bird.c's HxsprColor restore_ink
// (assumes a known, fixed ink baseline) remains valid wherever the bird
// flies, regardless of which band it's currently over.
//
// Pixel content is drawn BEFORE the colour-setting loop, not after: both
// draw pixel bytes across the FULL row width, including column-bytes 0-1,
// which hold each row's own ink/paper attribute bytes -- drawing pixel
// content after colour-setting would clobber those bytes back into stray
// pixel data (a real bug caught this way earlier in this file's history;
// colour-setting must always run LAST).
//
// Previously this file avoided row 199 and any pixel content near the
// creek/bottom of the screen, because of an interaction with
// hires_footer_enable() being active elsewhere (rendering every row's
// paper colour identically) -- see project memory
// project_phosphoric_footer_color_bug. That constraint no longer applies:
// main.c no longer calls hires_footer_enable() at all (see its own
// comment for why -- a separate, unrelated hardware aliasing issue with
// the bottom 24 scanlines), so this file is free to use the full 200-row
// height, including row 199.

#include <string.h>
#include "oric.h"
#include "hires.h"
#include "fixedmath.h"
#include "section_background.h"

#define SKY_PAPER        A_BGCYAN
#define BANK_PAPER       A_BGGREEN
#define RIVER_PAPER      A_BGBLUE

#define BANK_TOP         165u
#define RIVER_TOP        172u
#define RIVER_ROWS       (HIRES_ROWS - RIVER_TOP)   // 28 rows, 172..199

#define RIVER_SCROLL_EVERY  8u   // main-loop ticks between each 6px shimmer scroll step

static uint8_t tick_count = 0;

// A simple silhouette tree: a triangular canopy over a narrow trunk.
// base_y is the row the trunk's foot sits on.
static void draw_tree(const HiresBitmap *hb, uint8_t x, uint8_t base_y)
{
    hb_rect_fill(hb, (const HiresClip *)0, (uint8_t)(x - 1), (uint8_t)(base_y - 3), 3, 4, true);
    hb_triangle_fill(hb, (const HiresClip *)0,
                      x, (uint8_t)(base_y - 11),
                      (uint8_t)(x - 7), (uint8_t)(base_y - 3),
                      (uint8_t)(x + 7), (uint8_t)(base_y - 3),
                      true);
}

void section_background_run(const HiresBitmap *screen)
{
    uint8_t y;

    // Trees on the bank (pixel content -- see file header re: ordering).
    draw_tree(screen, 25, BANK_TOP + 6);
    draw_tree(screen, 125, BANK_TOP + 6);
    draw_tree(screen, 205, BANK_TOP + 6);

    // River shimmer texture: horizontal-stripe tile across the whole band.
    {
        static const uint8_t shimmer[8] = { 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00 };
        hb_rect_pattern(screen, (const HiresClip *)0, 0, RIVER_TOP, HIRES_WIDTH_PX, RIVER_ROWS, shimmer);
    }

    // Wavy river top edge: a gentle sine wave along the bank/river
    // boundary, for a natural (not dead-straight) waterline.
    {
        uint16_t x;
        for (x = 0; x < HIRES_WIDTH_PX; x++)
        {
            int16_t offset = ((int16_t)oric_sin((uint8_t)(x / 2)) * 3) / 127;
            uint8_t wave_y = (uint8_t)(RIVER_TOP - 2 + offset);
            hb_set(screen, (uint8_t)x, wave_y);
        }
    }

    // Colour -- LAST, so it isn't clobbered by any of the pixel-content
    // drawing above.
    for (y = 0; y < BANK_TOP; y++)
        hires_row_colors(y, A_FWWHITE, SKY_PAPER);
    for (y = BANK_TOP; y < RIVER_TOP; y++)
        hires_row_colors(y, A_FWWHITE, BANK_PAPER);
    for (y = RIVER_TOP; y < HIRES_ROWS; y++)
        hires_row_colors(y, A_FWWHITE, RIVER_PAPER);
}

// Scrolls the river band left by one column-byte, like
// section_clouds.c's own clouds_scroll_left() -- skips column-bytes 0-1
// (each row's own ink/paper attribute bytes) so they're never dragged
// into by the shifting shimmer texture. See that file's comment for the
// full rationale; identical mechanism, different band.
static void river_scroll_left(void)
{
    uint8_t y;
    for (y = RIVER_TOP; y < HIRES_ROWS; y++)
    {
        uint8_t *row = (uint8_t *)HIRESVRAM + (uint16_t)y * HIRES_ROW_BYTES;
        memmove(row + 2, row + 3, HIRES_ROW_BYTES - 3);
        row[HIRES_ROW_BYTES - 1] = row[2];   // wrap the shimmer tile's own period back in
    }
}

void section_background_tick(const HiresBitmap *screen)
{
    (void)screen;

    tick_count++;
    if (tick_count < RIVER_SCROLL_EVERY)
        return;
    tick_count = 0;

    river_scroll_left();
}
