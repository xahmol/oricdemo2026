// section_rasterirq_showcase.c - see section_rasterirq_showcase.h.
//
// 3 full-width colour bars (rows 0-199, the ENTIRE HIRES canvas -- unlike
// section_logo.c's own bars, confined to the logo's own row range)
// continuously cascade top-to-bottom, wrapping (a "conveyor", not a
// bounce), each cycling through a 7-colour palette as it travels. Driven
// directly from a NEW include/rasterirq.h __interrupt callback registered
// at 50Hz (Timer 1's own native rate) rather than from this section's own
// main-loop-paced tick() (~16.7Hz, see MAIN_FRAME_PACING_TICKS in
// main.c) -- see this file's own header comment for why that's a real,
// deliberately-chosen extra risk rather than a free win on this hardware.
//
// BACKDROP: 3 five-pointed stars (filled/hollow/patterned -- one each,
// showing off hb_line()+hb_flood_fill() in one section), drawn
// PROCEDURALLY via hires.h's own shape primitives at init() -- no
// picture asset at all, per explicit user direction ("draw some stars
// with our hires functions"), white ink on black paper only (matches the
// bars' own erase baseline exactly, see rb_step()'s comment below for why
// that keeps the whole design simple). star_dx[]/star_dy[] are
// PRECOMPUTED (Python, math.cos/sin) offsets for one canonical unit star
// shape (outer radius 34, inner radius 14) rather than computed at
// runtime -- this section never needs to rotate/resize the stars, so
// there's no reason to pull any trig call (or <math.h>) into this file at
// all. Each bar alternates "behind" (BAR_PHASE-style translucent -- only
// PAPER changes, ink stays the backdrop's own white baseline, so a star's
// white outline/fill stays visible through the colour wash) and "in
// front" (solid ink=paper, briefly hiding whatever's underneath) --
// per-bar, set once at init(), not dynamic -- giving a real mix of both
// styles across the 3 bars rather than every bar behaving identically.
//
// PERFORMANCE: arkos_tick() (Arkos's own 50Hz music decoder) runs from the
// SAME _hrirq_handler, sequentially, every single 20ms Timer 1 period --
// exactly the mechanism whose abuse already caused two real "music slows
// down" bugs in this project (most recently: section_func3d.c's original
// one-big-batch redesign this session). rb_step() below does bounded,
// O(1) work per bar per firing -- an incremental one-row SHIFT (erase the
// trailing row, paint the new leading row), never a full-thickness
// repaint -- specifically to keep this callback's own per-firing cost
// small and constant, unlike section_logo.c's paint_bar()/set_rows()
// (safe there only because that file's version runs from the main loop's
// own much larger ~60ms budget, not from inside this exact IRQ chain).
//
// CORRECTNESS: hrirq_add() has no "remove callback" primitive (the
// callback table in rasterirq.c is append-only) -- so once registered,
// this callback would otherwise keep firing and overwriting row colours
// every 20ms FOREVER, corrupting every later section, regardless of
// whether this section ends naturally or gets skipped early (keypress or
// max_ticks). Guarded by rb_active, a plain volatile flag checked first
// in the callback (an instant no-op when false) -- see
// section_rasterirq_showcase_deactivate() below and src/main.c's
// transition_clear(), which calls it unconditionally after EVERY
// section's run_section() returns (a no-op for all the others).
//
// Also guarded: hrirq_add() itself is only ever called ONCE, on the
// FIRST pass through this section (rb_registered) -- main.c's own outer
// loop cycles through the whole sections[] table forever, so this
// section's own init() runs again every lap of the demo; without this
// guard, every subsequent lap would register ANOTHER duplicate callback
// entry (HRIRQ_MAX_CALLBACKS is only 8), eventually running the same
// bar logic multiple times per firing and then silently failing to
// register at all once the table filled up.
//
// __noinline discipline: rb_step() is not itself a loop over
// hires_row_off[y] (unlike section_logo.c's paint_bar()/set_rows(), whose
// own header comment documents the exact Oscar64 -O2 INLINER bug that
// requires it there), but is marked __noinline anyway as the same cheap
// defense-in-depth section_logo.c's own bar_advance() already applies to
// every function in that file operating on per-row colour state, given
// this project's now-repeated history of whole-program-optimizer bugs in
// this general code shape (~/.claude/oscar64.md).

#include <stdbool.h>
#include "oric.h"
#include "hires.h"
#include "rasterirq.h"
#include "section_rasterirq_showcase.h"
#include "section_common.h"

#define NUM_BARS             3u
#define BAR_THICKNESS        16u
// Firings (50Hz ticks) between one colour-cycle step -- ~0.64s/step.
#define COLOR_CYCLE_FIRINGS  32u
// Full top-to-bottom passes (each pass == HIRES_ROWS firings, ~4s at
// 50Hz) before this section calls section_mark_finished() -- a real
// natural end, like section_splash.c's own fade-out completing.
#define TOTAL_PASSES         4u

#define RASTERBAR_ISR_OFFSET 20u   // delay units since the previous callback fired -- see rasterirq.h

static const uint8_t rb_palette[] = {
    A_BGRED, A_BGGREEN, A_BGYELLOW, A_BGBLUE, A_BGMAGENTA, A_BGCYAN, A_BGWHITE
};
#define RB_PALETTE_SIZE (sizeof(rb_palette) / sizeof(rb_palette[0]))

typedef struct
{
    uint8_t head;         // current leading (bottom-most) row, 0..HIRES_ROWS-1
    uint8_t color_index;  // index into rb_palette
    uint8_t color_timer;  // firings remaining until the next colour-cycle step
    bool    behind;       // true: paper-only ("behind"); false: solid ink=paper ("in front")
} RasterBar;

static RasterBar bars[NUM_BARS];

// One canonical 5-pointed star's 10 vertices (5 outer + 5 inner,
// alternating), as (dx,dy) offsets from a centre point -- outer radius
// 34, inner radius 14, point-up. Precomputed once (Python: math.cos/sin,
// angle = i*(pi/5) - pi/2), not at runtime -- see this file's own header
// comment for why.
static const int8_t star_dx[10] = {   0,   8,  32,  13,  20,   0, -20, -13, -32,  -8 };
static const int8_t star_dy[10] = { -34, -11, -11,   4,  28,  14,  28,   4, -11, -11 };

#define STAR_R 34u   // outer radius -- used for the patterned star's hatch-line span

typedef enum { STAR_FILLED, STAR_HOLLOW, STAR_PATTERNED } StarStyle;

// Draws the star's own 10-segment outline -- shared by all 3 styles
// (STAR_FILLED/STAR_PATTERNED both flood-fill the interior this encloses
// afterward, see draw_star() below).
static void draw_star_outline(const HiresBitmap *screen, const uint8_t *xs, const uint8_t *ys)
{
    uint8_t i;
    for (i = 0; i < 10; i++)
    {
        uint8_t j = (uint8_t)((i + 1u) % 10u);
        hb_line(screen, (const HiresClip *)0, xs[i], ys[i], xs[j], ys[j], true);
    }
}

// Draws one star centred at (cx,cy). White ink, no fill colour choice --
// the backdrop is deliberately white-on-black only (see this file's own
// header comment: keeps the bars' own erase-restore trivial, just the
// same fixed A_FWWHITE/A_BGBLACK baseline every row already uses).
//
// Fill/pattern styles use hb_flood_fill() from a centre seed, NOT
// hb_polygon_fill() -- a real bug found via soak-test screenshots showed
// hb_polygon_fill() (hires.c's own per-pixel x per-edge point-in-polygon
// test, WITH A DIVISION in its innermost loop) taking several real
// SECONDS to fill one ~68px star on this 1MHz CPU, visibly growing the
// star on screen over that whole time and delaying this section's first
// bar-sweep tick by that much. hb_flood_fill() is a scanline-stack
// algorithm with no division at all (see its own header comment in
// hires.c) -- dramatically faster for a solid interior fill, and the
// star's own closed outline (drawn first) is exactly the boundary a
// flood fill needs.
static void draw_star(const HiresBitmap *screen, uint8_t cx, uint8_t cy, StarStyle style)
{
    uint8_t xs[10], ys[10];
    uint8_t i;

    for (i = 0; i < 10; i++)
    {
        xs[i] = (uint8_t)(cx + star_dx[i]);
        ys[i] = (uint8_t)(cy + star_dy[i]);
    }

    draw_star_outline(screen, xs, ys);

    switch (style)
    {
    case STAR_HOLLOW:
        break;   // outline only, already drawn above

    case STAR_FILLED:
        hb_flood_fill(screen, (const HiresClip *)0, cx, cy, true);   // seed: the star's own centre, always inside
        break;

    case STAR_PATTERNED:
    {
        // Fill solid first, then punch evenly-spaced horizontal gap
        // lines across the star's own bounding box -- a plain hatched
        // look, reusing only hb_flood_fill()/hb_line() (no new primitive
        // needed).
        uint8_t y;
        hb_flood_fill(screen, (const HiresClip *)0, cx, cy, true);
        for (y = (uint8_t)(cy - STAR_R + 2u); y < (uint8_t)(cy + STAR_R - 1u); y = (uint8_t)(y + 4u))
            hb_line(screen, (const HiresClip *)0, (uint8_t)(cx - STAR_R), y, (uint8_t)(cx + STAR_R), y, false);
        break;
    }
    }
}

// Written only from rasterbar_isr()'s own __interrupt context, read only
// from section_rasterirq_showcase_tick() -- same single-byte-write
// convention as main.c's own main_frame_tick, safe without locking on a
// 6502 (single-byte reads/writes are atomic).
static volatile bool    rb_active;
static volatile uint8_t rb_passes;
static bool              rb_registered;   // hrirq_add() called at most once, ever

// Shifts one bar down by exactly one row: erases the row it's about to
// uncover (restores the blank white-ink/black-paper baseline
// transition_clear() leaves), advances head, paints the newly-covered
// row in the bar's current colour, then counts down to its next colour
// change. No division/modulo anywhere (an interrupt-context 16-bit
// division on 6502 would be far too slow for a per-firing cost) --
// wraparound is a plain compare-and-adjust instead.
__noinline static void rb_step(RasterBar *bar)
{
    int16_t exited;
    uint8_t exited_row;
    uint8_t paper, ink;

    exited = (int16_t)bar->head - (int16_t)BAR_THICKNESS + 1;
    if (exited < 0)
        exited += (int16_t)HIRES_ROWS;
    exited_row = (uint8_t)exited;

    bar->head = (uint8_t)(bar->head + 1u);
    if (bar->head >= HIRES_ROWS)
        bar->head = 0;

    // Erase always restores the SAME fixed white-ink/black-paper baseline
    // regardless of what's underneath (a star's own outline/fill pixels
    // are untouched -- this only ever rewrites the ROW's ink/paper
    // attribute bytes at columns 0-1, never pixel data) -- safe precisely
    // because the backdrop is white-on-black only, per this file's own
    // header comment.
    hires_row_colors(exited_row, A_FWWHITE, A_BGBLACK);

    paper = rb_palette[bar->color_index];
    if (bar->behind)
    {
        // "Behind": ink stays the backdrop's own white baseline, only
        // paper changes -- any star pixels in this row stay visible
        // (tinted by the new paper colour around them), same "translucent"
        // framing as section_logo.c's own BAR_PHASE_DOWN.
        ink = A_FWWHITE;
    }
    else
    {
        // "In front": solid, ink=paper -- same derivation section_logo.c's
        // paint_bar() uses (palette[] holds A_BG*/16-23 values; ink needs
        // the matching A_FW*/0-7 code).
        ink = (uint8_t)(paper - 16u);
    }
    hires_row_colors(bar->head, ink, paper);

    if (bar->color_timer == 0)
    {
        bar->color_index = (uint8_t)(bar->color_index + 1u);
        if (bar->color_index >= RB_PALETTE_SIZE)
            bar->color_index = 0;
        bar->color_timer = COLOR_CYCLE_FIRINGS;
    }
    else
    {
        bar->color_timer = (uint8_t)(bar->color_timer - 1u);
    }
}

__interrupt void rasterbar_isr(void)
{
    if (!rb_active)
        return;

    rb_step(&bars[0]);
    if (bars[0].head == 0)
        rb_passes = (uint8_t)(rb_passes + 1u);
    rb_step(&bars[1]);
    rb_step(&bars[2]);
}

// Star centres -- 3 stars spread across the canvas, well clear of the
// reserved column-bytes 0-1 (12px) on the left (leftmost star's own
// bounding box: cx=55-34=21) and the right edge (rightmost: cx=185+34=219,
// under HIRES_WIDTH_PX=240).
#define STAR1_CX 55u
#define STAR2_CX 120u
#define STAR3_CX 185u
#define STAR_CY  100u

void section_rasterirq_showcase_init(const HiresBitmap *screen)
{
    uint8_t i;

    draw_star(screen, STAR1_CX, STAR_CY, STAR_FILLED);
    draw_star(screen, STAR2_CX, STAR_CY, STAR_HOLLOW);
    draw_star(screen, STAR3_CX, STAR_CY, STAR_PATTERNED);

    for (i = 0; i < NUM_BARS; i++)
    {
        bars[i].head        = (uint8_t)(((uint16_t)i * HIRES_ROWS) / NUM_BARS);
        bars[i].color_index = i;
        bars[i].color_timer = COLOR_CYCLE_FIRINGS;
        bars[i].behind       = (i % 2u) == 0u;   // bars 0,2 behind; bar 1 in front
    }
    rb_passes = 0;
    rb_active = true;

    if (!rb_registered)
    {
        hrirq_add(RASTERBAR_ISR_OFFSET, rasterbar_isr);
        rb_registered = true;
    }
}

void section_rasterirq_showcase_tick(const HiresBitmap *screen)
{
    (void)screen;
    if (rb_passes >= TOTAL_PASSES)
        section_mark_finished();
}

void section_rasterirq_showcase_deactivate(void)
{
    rb_active = false;
}
