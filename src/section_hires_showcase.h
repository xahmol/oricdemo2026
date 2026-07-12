// section_hires_showcase.h - HIRES shape-drawing primitives showcase
//
// Demonstrates hires.h's fill primitives (ellipse, polygon/star, pattern
// hatch, flood fill) building up on screen one at a time, each in its own
// non-overlapping horizontal band (so each can have its own plain ink
// colour via hires_row_colors() with no ink-bracket collision risk --
// see section_bird.c's own HxsprColor for why that matters when regions
// DO share rows).
//
// Also where the demo's second music track (assets/boulesetbits.aky)
// takes over from the first (assets/steppingout.aky) -- see
// section_hires_showcase.c's own header comment.

#ifndef SECTION_HIRES_SHOWCASE_H
#define SECTION_HIRES_SHOWCASE_H

#include <stdbool.h>
#include "hires.h"

void section_hires_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_hires_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_hires_showcase.c")

#endif // SECTION_HIRES_SHOWCASE_H
