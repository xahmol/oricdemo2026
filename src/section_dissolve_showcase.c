// section_dissolve_showcase.c - see section_dissolve_showcase.h.
//
// Deliberately does NOT #include "dissolve.h": pulling include/dissolve.c
// into this program once already caused a real, confirmed regression --
// see src/main.c's own transition_clear() comment for the full account
// (a real Phosphoric/Oricutron soak test showed Oscar64's -O2 whole-
// program register allocator pushing arkos_tick(), a real __interrupt
// handler, right up against Oscar64's own documented interrupt-complexity
// limit once dissolve.c's compilation unit was linked in, silently
// corrupting Arkos playback rather than hard-erroring). The program has
// only grown since that fix (four more sections), so the risk is not
// smaller today -- both dissolve.h techniques are reimplemented locally
// here instead, using only primitives ALREADY linked into every build
// (hires_row_colors(), hb_put()):
//
//   1. A stride-strobed attribute fade (stride 8->4->2->1), matching
//      hires_row_colors_range()'s own algorithm exactly, via a plain loop
//      calling hires_row_colors() directly -- the same technique
//      main.c's own transition_clear() already uses for the identical
//      reason.
//   2. A 16-bit Galois LFSR pixel reveal (polynomial 0xB400, the same one
//      dissolve.c uses), driving hb_put() to progressively reveal a solid
//      colour in pseudo-random pixel order.
//
// Both are safe to run here specifically BECAUSE the canvas is blank and
// flat when this section's own init() runs: main.c's own transition_clear()
// always wipes the previous section's content and resets to a plain
// white-ink/black-paper baseline before ANY section's init() executes (see
// main.c's own main loop). A stride-attribute-fade or per-pixel reveal
// applied directly to one of this project's busy PHOTOGRAPHIC pictures
// (real per-row attribute bytes scattered at many different columns, not
// just a couple of fixed ink-bracket columns) would NOT be safe this way --
// hires_row_colors() only ever touches column-bytes 0/1, so any OTHER
// attribute byte later in that same row would immediately override it, and
// hb_put() unconditionally treats its target as pixel data (sets bit6),
// which would silently convert an existing ATTRIBUTE byte at that column
// into pixel data, destroying whatever colour change it was making for the
// rest of that row. Neither hazard applies to a canvas that's genuinely
// blank going in.

#include "oric.h"
#include "hires.h"
#include "section_dissolve_showcase.h"
#include "section_common.h"

// Phase 1: stride-strobed fade, from the blank white-ink/black-paper
// baseline transition_clear() leaves behind, to a solid cyan-ink/blue-
// paper backdrop for phase 2's own reveal.
#define FADE_INK    A_FWCYAN
#define FADE_PAPER  A_BGBLUE
static const uint8_t fade_strides[] = { 8, 4, 2, 1 };
#define NUM_FADE_STRIDES (sizeof(fade_strides) / sizeof(fade_strides[0]))

static void apply_stride(uint8_t stride)
{
    uint8_t y;
    for (y = 0; y < HIRES_ROWS; y = (uint8_t)(y + stride))
        hires_row_colors(y, FADE_INK, FADE_PAPER);
}

// Phase 2: LFSR pixel reveal -- same polynomial/algorithm as
// include/dissolve.c's own hires_dissolve_init/next (see this file's own
// header comment for why it's a local copy, not a call into that file).
#define _DISSOLVE_PIXEL_COUNT ((uint16_t)((uint32_t)HIRES_WIDTH_PX * HIRES_ROWS))
#define DISSOLVE_SEED         12345u
#define PIXELS_PER_TICK       400u

static uint16_t dissolve_lfsr;
static uint16_t pixels_revealed;

static uint16_t dissolve_next(void)
{
    uint16_t val;
    do
    {
        uint16_t lsb = (uint16_t)(dissolve_lfsr & 1);
        dissolve_lfsr = (uint16_t)(dissolve_lfsr >> 1);
        if (lsb)
            dissolve_lfsr = (uint16_t)(dissolve_lfsr ^ 0xB400);
        val = dissolve_lfsr;
    } while (val >= _DISSOLVE_PIXEL_COUNT);
    return val;
}

typedef enum { STAGE_FADE, STAGE_REVEAL } DissolveStage;
static DissolveStage stage;
static uint8_t fade_step;

void section_dissolve_showcase_init(const HiresBitmap *screen)
{
    stage = STAGE_FADE;
    fade_step = 0;
    dissolve_lfsr = DISSOLVE_SEED;
    pixels_revealed = 0;
}

// void, not bool -- see section_common.h's own header comment for why.
// Calls section_mark_finished() once the reveal completes -- this
// section has a real, natural end (unlike the wave/scroll sections either
// side of it), so main.c advances into section_macaw_showcase as soon as
// it's done rather than waiting out its own max_ticks.
void section_dissolve_showcase_tick(const HiresBitmap *screen)
{
    if (stage == STAGE_FADE)
    {
        apply_stride(fade_strides[fade_step]);
        fade_step++;
        if (fade_step >= NUM_FADE_STRIDES)
            stage = STAGE_REVEAL;
        return;
    }

    {
        uint16_t i;
        for (i = 0; i < PIXELS_PER_TICK && pixels_revealed < _DISSOLVE_PIXEL_COUNT; i++)
        {
            uint16_t position = dissolve_next();
            uint8_t y = (uint8_t)(position / HIRES_WIDTH_PX);
            uint8_t x = (uint8_t)(position % HIRES_WIDTH_PX);
            hb_put(screen, x, y, true);
            pixels_revealed++;
        }
        if (pixels_revealed >= _DISSOLVE_PIXEL_COUNT)
            section_mark_finished();
    }
}
