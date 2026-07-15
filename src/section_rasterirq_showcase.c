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
// natural end, like section_dissolve_showcase's own reveal-completion.
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
} RasterBar;

static RasterBar bars[NUM_BARS];

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

    hires_row_colors(exited_row, A_FWWHITE, A_BGBLACK);

    paper = rb_palette[bar->color_index];
    // palette[] holds A_BG* (paper-range, 16-23) values -- ink needs the
    // matching A_FW* (0-7) code for a solid fully-opaque bar, same
    // paper-to-ink derivation section_logo.c's paint_bar() already uses.
    ink = (uint8_t)(paper - 16u);
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

void section_rasterirq_showcase_init(const HiresBitmap *screen)
{
    uint8_t i;
    (void)screen;

    for (i = 0; i < NUM_BARS; i++)
    {
        bars[i].head        = (uint8_t)(((uint16_t)i * HIRES_ROWS) / NUM_BARS);
        bars[i].color_index = i;
        bars[i].color_timer = COLOR_CYCLE_FIRINGS;
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
