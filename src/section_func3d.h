// section_func3d.h - wireframe 3D height-field surface
//
// Inspired by Oscar64's own samples/hires/func3d.c: a rippling surface
// function (f = -cos(r*16) * exp(-2*r)) projected through a perspective
// camera via gfx/vector3d.h, built up across several ticks with on-screen
// status captions ("Preparing function" / "Projecting vertices" /
// "Drawing surfaces" -- func3d.c's own fourth caption, "Sorting
// surfaces", is dropped along with the depth-sort/quad-fill it
// described, see section_func3d.c's own header comment for why), then
// continuously slowly rotated for the rest of the section's hold time.

#ifndef SECTION_FUNC3D_H
#define SECTION_FUNC3D_H

#include <stdbool.h>
#include "hires.h"

void section_func3d_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_func3d_tick(const HiresBitmap *screen);

#pragma compile("section_func3d.c")

#endif // SECTION_FUNC3D_H
