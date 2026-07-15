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
// base_y is the row the trunk's foot sits on. TREE_HALF_SPAN/TREE_HEIGHT
// describe the canopy's footprint -- the widest part of the whole tree,
// used below to bracket it into black ink (see draw_tree_ink()).
#define TREE_HALF_SPAN   13u    // canopy is (2*TREE_HALF_SPAN+1)px wide
#define TREE_HEIGHT      20u    // canopy apex sits TREE_HEIGHT rows above base_y
#define TREE_TRUNK_HALF   2u    // trunk is (2*TREE_TRUNK_HALF+1)px wide
#define TREE_TRUNK_ROWS   7u

// The canopy is drawn as stacked hb_rect_fill() bands (a stepped/pine
// silhouette), not a filled triangle -- `hb_triangle_fill`/`hb_polygon_fill`
// (tried first) had a real, unresolved Oscar64 -O2 whole-program
// register-allocator bug (matching ~/.claude/oscar64.md's documented
// "caller-save set can be under-counted" class) that made its own nested
// fill loop silently skip most of its iterations, but ONLY when called
// from this deep in the real demo's call graph (main -> run_section ->
// ... -> draw_tree): confirmed via RAM-dump instrumentation that only 149
// of an expected 405 point tests ran (0 ever "inside"), while an isolated
// call with the IDENTICAL triangle coordinates rendered correctly.
// That function was later removed from the library entirely (a second,
// separate problem found independently -- a division-heavy inner loop
// made it severely slow even when it DID work correctly -- see hires.h's
// own header comment), so this is no longer a live risk to route around,
// but the bands stay: hb_rect_fill's much simpler loop is proven correct
// here, and a smooth-triangle canopy was never load-bearing to begin with.
#define TREE_CANOPY_BANDS      5u
#define TREE_CANOPY_BAND_ROWS  3u   // 5*3 = 15 rows, matching the old triangle's
                                    // actual filled height (apex to base_y-TREE_TRUNK_ROWS+1)

static void draw_tree(const HiresBitmap *hb, uint8_t x, uint8_t base_y)
{
    uint8_t apex_y = (uint8_t)(base_y - TREE_HEIGHT);
    uint8_t band;

    hb_rect_fill(hb, (const HiresClip *)0, (uint8_t)(x - TREE_TRUNK_HALF), (uint8_t)(base_y - TREE_TRUNK_ROWS + 1),
                 2 * TREE_TRUNK_HALF + 1, TREE_TRUNK_ROWS, true);

    for (band = 0; band < TREE_CANOPY_BANDS; band++)
    {
        uint8_t half_width = (uint8_t)((TREE_HALF_SPAN * (band + 1)) / TREE_CANOPY_BANDS);
        hb_rect_fill(hb, (const HiresClip *)0,
                     (uint8_t)(x - half_width), (uint8_t)(apex_y + band * TREE_CANOPY_BAND_ROWS),
                     (uint8_t)(2 * half_width + 1), TREE_CANOPY_BAND_ROWS, true);
    }
}

// Colours a tree black (silhouette-style, matching section_bird.c's own
// black-ink bird convention -- avoids a white-tree/black-bird clash) via an
// ink-only bracket, the same technique as sprite.h's HxsprColor, but
// applied ONCE here since trees are static (never move, never erased): one
// column-byte immediately before the tree's leftmost pixel column is set to
// black ink, and one immediately after its rightmost pixel column is set
// back to white (the whole-screen ink baseline -- see section_background_run()'s
// own header comment), for every row the tree's bounding box spans. Must
// run AFTER section_background_run()'s baseline hires_row_colors() sweep,
// or that sweep's own col0/col1 ink write would simply overwrite this.
// The three trees' column ranges never overlap (see call sites below), so
// each tree's bracket is independent, same as a non-moving HxsprColor.
static void draw_tree_ink(uint8_t x, uint8_t base_y)
{
    uint8_t left_col = (uint8_t)((x - TREE_HALF_SPAN) / 6u);    // 6px per column-byte
    uint8_t right_col = (uint8_t)((x + TREE_HALF_SPAN) / 6u);
    uint8_t y;
    for (y = (uint8_t)(base_y - TREE_HEIGHT); y <= base_y; y++)
    {
        hires_put_ink((uint8_t)(left_col - 1), y, A_FWBLACK);
        hires_put_ink((uint8_t)(right_col + 1), y, A_FWWHITE);
    }
}

// The three trees' x positions -- 31/125/199 keep each tree's ink bracket
// (see draw_tree_ink()) clear of column-bytes 0-1 (the row's own baseline
// ink/paper attribute pair) and clear of each other -- the widest
// (TREE_HALF_SPAN=13px) canopy's bracket columns land at 2/8, 17/24,
// 30/36 respectively, all comfortably spaced.
static const uint8_t tree_x[3] = { 31, 125, 199 };

void section_background_run(const HiresBitmap *screen)
{
    uint8_t y;

    // Trees on the bank (pixel content -- see file header re: ordering).
    draw_tree(screen, tree_x[0], BANK_TOP + 6);
    draw_tree(screen, tree_x[1], BANK_TOP + 6);
    draw_tree(screen, tree_x[2], BANK_TOP + 6);

    // River shimmer: several wavy, DASHED lines at different depths within
    // the band, each with its own sine phase and dash length -- replaces an
    // earlier straight, uninterrupted hb_rect_pattern band (looked like
    // ruled paper, not water; see git history/project feedback). Each
    // line's y wobbles gently (+/-2px) via oric_sin at its own phase, and
    // is only drawn every-other dash-length run (the "& 1" below), giving
    // broken segments rather than a solid line -- varying the phase/period
    // per line keeps the breaks from lining up across lines, for a more
    // random, natural shimmer look.
    {
        static const uint8_t line_row[]    = { 3, 9, 15, 21 };     // depth within the band (0..RIVER_ROWS-1)
        static const uint8_t line_phase[]  = { 0, 40, 90, 160 };   // stagger each line's sine phase
        static const uint8_t line_period[] = { 18, 26, 22, 30 };   // dash length varies per line too
        uint8_t line;
        for (line = 0; line < 4; line++)
        {
            uint16_t x;
            for (x = 0; x < HIRES_WIDTH_PX; x++)
            {
                if (((x / line_period[line]) & 1) != 0)
                    continue;   // gap between dashes
                int16_t offset = ((int16_t)oric_sin((uint8_t)(x / 3 + line_phase[line])) * 2) / 127;
                uint8_t wave_y = (uint8_t)(RIVER_TOP + line_row[line] + offset);
                hb_set(screen, (uint8_t)x, wave_y);
            }
        }
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

    // Tree ink brackets -- AFTER the baseline sweep above, or it would
    // simply overwrite these (see draw_tree_ink()'s own comment).
    draw_tree_ink(tree_x[0], BANK_TOP + 6);
    draw_tree_ink(tree_x[1], BANK_TOP + 6);
    draw_tree_ink(tree_x[2], BANK_TOP + 6);
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
