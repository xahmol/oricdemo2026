// section_bird.h - animated walking-bird demo section (HIRES mode)
//
// A nod to the animated bird in the original "Welcome to Oric Atmos" demo
// (oric.org/software/welcome_to_oric_atmos-593.html). Frame data is
// assets/bird.h -- see that file for provenance/attribution. Drawn via a
// direct byte-level XOR sprite (see section_bird.c) rather than
// sprite.h's HiresSprite -- much faster for a 66x64 sprite than
// hb_bitblit's per-pixel save-under approach.

#ifndef SECTION_BIRD_H
#define SECTION_BIRD_H

#include "hires.h"

// Runs the bird walk-cycle animation on `screen` (pass the live HIRES
// canvas, e.g. from hb_init(&screen, (uint8_t *)HIRESVRAM, HIRES_ROWS)).
// Loops forever -- this is currently the demo's only section.
void section_bird_run(const HiresBitmap *screen);

#pragma compile("section_bird.c")

#endif // SECTION_BIRD_H
