// section_dissolve_showcase.h - Phase 7's dissolve/fade showcase
//
// Bridges section_wave_showcase.c (the magazine-photo wave) into
// section_macaw_showcase.c (the macaw scroll): a stride-strobed
// attribute fade followed by a pseudo-random pixel reveal, both
// techniques deliberately reimplemented LOCALLY rather than using
// include/dissolve.h/.c -- see section_dissolve_showcase.c's own header
// comment for why (a real, documented Arkos-corrupting regression from
// pulling dissolve.c into this exact whole-program build, previously
// found and fixed -- see src/main.c's own transition_clear() comment).

#ifndef SECTION_DISSOLVE_SHOWCASE_H
#define SECTION_DISSOLVE_SHOWCASE_H

#include "hires.h"

void section_dissolve_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_dissolve_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_dissolve_showcase.c")

#endif // SECTION_DISSOLVE_SHOWCASE_H
