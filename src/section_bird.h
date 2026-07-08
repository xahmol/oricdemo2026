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

// Draws the bird's first frame at its starting position. Call once before
// any section_bird_tick() calls.
void section_bird_init(const HiresBitmap *screen);

// Advances the bird one animation step (erase old position/frame, advance
// state, draw new position/frame) and busy-waits BIRD_STEP_DELAY ticks --
// call in a loop, e.g. alongside other sections' own per-frame tick
// functions (see main.c's master loop).
void section_bird_tick(const HiresBitmap *screen);

#pragma compile("section_bird.c")

#endif // SECTION_BIRD_H
