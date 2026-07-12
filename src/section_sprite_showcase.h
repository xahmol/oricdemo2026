// section_sprite_showcase.h - sprite.h showcase: a small satellite drifting
// across a real, non-blank starfield backdrop
//
// Demonstrates hxspr_draw()/hxspr_erase()'s HXSPR_OR mode's byte-exact
// backup/restore over REAL background content -- the exact scenario
// section_bird.c no longer needs now that it flies only over blank sky
// (see that file's own header comment). Loads assets/starfield.bin (a
// sparse night-sky picture, see picture.h) at runtime as the backdrop,
// then moves assets/satellite.h's small sprite across it in column-byte
// steps, wrapping around at the edges.

#ifndef SECTION_SPRITE_SHOWCASE_H
#define SECTION_SPRITE_SHOWCASE_H

#include <stdbool.h>
#include "hires.h"

void section_sprite_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_sprite_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_sprite_showcase.c")

#endif // SECTION_SPRITE_SHOWCASE_H
