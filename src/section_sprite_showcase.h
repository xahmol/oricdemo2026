// section_sprite_showcase.h - sprite.h showcase: a small satellite drifting
// across a procedurally-generated, continuously-scrolling starfield
//
// Demonstrates hxspr_draw()/hxspr_erase()'s HXSPR_OR mode's byte-exact
// backup/restore over REAL background content -- the exact scenario
// section_bird.c no longer needs now that it flies only over blank sky
// (see that file's own header comment). The starfield itself is drawn
// and animated directly via hires.h's own hb_set()/hb_clr() (no picture
// asset at all, per explicit user direction -- "just random dots" don't
// need a converted image), scrolling slower than the satellite for a
// two-speed depth-parallax effect -- same convention as
// section_clouds.c's own cloud layer against the bird's own movement.

#ifndef SECTION_SPRITE_SHOWCASE_H
#define SECTION_SPRITE_SHOWCASE_H

#include <stdbool.h>
#include "hires.h"

void section_sprite_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_sprite_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_sprite_showcase.c")

#endif // SECTION_SPRITE_SHOWCASE_H
