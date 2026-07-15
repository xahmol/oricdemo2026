// section_dissolve_showcase.h - Phase 7's dissolve/fade showcase
//
// Bridges section_wave_showcase.c (the magazine-photo wave) into
// section_macaw_showcase.c (the macaw scroll): loads both real pictures
// itself (assets/oricmag.bin as the starting frame, assets/macaw.bin
// revealed top-down via repeated growing-size picture_load() calls) --
// see section_dissolve_showcase.c's own header comment for the Round 12
// redesign rationale (the original stride-fade + LFSR-reveal version
// never actually showed a transition between two real pictures, since
// main.c's own transition_clear() wipe left this section's own canvas
// blank going in).

#ifndef SECTION_DISSOLVE_SHOWCASE_H
#define SECTION_DISSOLVE_SHOWCASE_H

#include "hires.h"

void section_dissolve_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_dissolve_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_dissolve_showcase.c")

#endif // SECTION_DISSOLVE_SHOWCASE_H
