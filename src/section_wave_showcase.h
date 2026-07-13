// section_wave_showcase.h - horizontal sine-wave picture distortion
//
// A vintage magazine scan of a full Oric Atmos setup (assets/oricmag.bin),
// loaded at runtime via include/picture.h, continuously distorted with a
// banded horizontal sine wave (see section_wave_showcase.c's own header
// comment for why banded, not per-scanline, and why row ROTATION rather
// than a plain shift-with-fill).

#ifndef SECTION_WAVE_SHOWCASE_H
#define SECTION_WAVE_SHOWCASE_H

#include "hires.h"

void section_wave_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_wave_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_wave_showcase.c")

#endif // SECTION_WAVE_SHOWCASE_H
