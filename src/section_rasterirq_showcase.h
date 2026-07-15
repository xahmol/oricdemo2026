// section_rasterirq_showcase.h - Phase 8's standalone raster-bar showcase
//
// Distinct from section_logo.c's own subtler circling-bar accent (a
// per-tick, main-loop-paced static write -- ~16.7Hz, see
// MAIN_FRAME_PACING_TICKS in main.c): this section drives its bars
// directly from a NEW include/rasterirq.h __interrupt callback, at the
// full 50Hz Timer 1 tick rate, for genuinely smoother motion. Oric has no
// live hardware colour register and no true raster interrupts (every
// colour is an embedded VRAM attribute byte, safe to write ahead of time
// -- see ~/.claude/oric_atmos_reference.md), so this buys update RATE, not
// intra-frame colour-split precision -- a real, deliberately-chosen extra
// risk (see section_rasterirq_showcase.c's own header comment for the
// correctness/performance hazards this design has to guard against).

#ifndef SECTION_RASTERIRQ_SHOWCASE_H
#define SECTION_RASTERIRQ_SHOWCASE_H

#include "hires.h"

void section_rasterirq_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_rasterirq_showcase_tick(const HiresBitmap *screen);

// Disarms the __interrupt callback registered by _init() above (a plain
// flag write -- cheap and safe to call unconditionally, even when this
// section was never entered or already deactivated). MUST be called
// after this section ends, however it ends (natural end, keypress skip,
// or max_ticks force-advance) -- hrirq_add() has no "remove callback"
// primitive, so a still-armed callback would otherwise keep overwriting
// row colours every 20ms forever, corrupting every later section. See
// src/main.c's transition_clear(), which calls this unconditionally
// after EVERY section (a no-op for all the others).
void section_rasterirq_showcase_deactivate(void);

#pragma compile("section_rasterirq_showcase.c")

#endif // SECTION_RASTERIRQ_SHOWCASE_H
