// section_polygon_workout.h - continuously rotating/pulsing wireframe star
//
// Inspired by Oscar64's own samples/hires/polygon.c (a 10-point star
// polygon rotated and scaled through 128 frames, filled with a shifting
// grey shade each frame) -- reimagined as a WIREFRAME-only effect for the
// Oric (no per-pixel/per-cell colour to shade a fill with, see hires.h's
// own header comment on ink/paper being a per-row-span serial attribute,
// not true per-pixel colour), and made continuously animated rather than
// a one-shot 128-frame sequence, since this section has no natural end
// (paced only by main.c's own min_ticks/max_ticks, matching
// section_logo.c's own circling raster bars).

#ifndef SECTION_POLYGON_WORKOUT_H
#define SECTION_POLYGON_WORKOUT_H

#include <stdbool.h>
#include "hires.h"

void section_polygon_workout_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_polygon_workout_tick(const HiresBitmap *screen);

#pragma compile("section_polygon_workout.c")

#endif // SECTION_POLYGON_WORKOUT_H
