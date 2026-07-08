// section_clouds.h - parallax cloud layer, upper sky band (HIRES mode)

#ifndef SECTION_CLOUDS_H
#define SECTION_CLOUDS_H

#include "hires.h"

// Draws the initial clouds. Call once before any section_clouds_tick()
// calls, after section_background_run() has painted the sky.
void section_clouds_init(const HiresBitmap *screen);

// Advances the cloud layer: scrolls it left by one column-byte every
// CLOUD_SCROLL_EVERY calls (see section_clouds.c), spawning a fresh cloud
// at the right edge periodically so the band never runs empty. Call once
// per main-loop tick, alongside other sections' own tick functions (see
// main.c).
void section_clouds_tick(const HiresBitmap *screen);

#pragma compile("section_clouds.c")

#endif // SECTION_CLOUDS_H
