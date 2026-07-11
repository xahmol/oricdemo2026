// section_background.h - static sky/bank/trees + animated river (HIRES mode)

#ifndef SECTION_BACKGROUND_H
#define SECTION_BACKGROUND_H

#include "hires.h"

// Draws the sky, green bank, trees, and river once. Call before any section
// that draws on top of it (e.g. section_bird_run()).
void section_background_run(const HiresBitmap *screen);

// Advances the river's shimmer animation one step -- call once per
// main-loop tick, alongside other sections' own tick functions (see
// main.c). Paces itself internally (see RIVER_SCROLL_EVERY in
// section_background.c), same convention as section_clouds_tick().
void section_background_tick(const HiresBitmap *screen);

#pragma compile("section_background.c")

#endif // SECTION_BACKGROUND_H
