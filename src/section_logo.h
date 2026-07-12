// section_logo.h - HIRES Oric logo with circling raster-style bars
//
// Loads assets/oriclogo.bin (a real oric.org "Welcome to Oric Atmos"
// screenshot, cropped to just the ORIC/ATMOS 48K wordmark and converted
// via tools/oric_pictconv.py --mode mono, forced white ink) at runtime
// via include/picture.h, then animates a single highlight bar circling
// through the wordmark's own row range: sweeping top-to-bottom UNDER the
// artwork (rewriting the PAPER attribute, i.e. behind it), then
// bottom-to-top OVER it (rewriting the INK attribute, i.e. in front of
// it), repeating indefinitely -- see section_logo.c's own header comment
// for why this uses plain per-tick attribute rewrites rather than
// include/rasterirq.h's mid-frame IRQ callbacks (a design call, not an
// oversight).

#ifndef SECTION_LOGO_H
#define SECTION_LOGO_H

#include <stdbool.h>
#include "hires.h"

void section_logo_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_logo_tick(const HiresBitmap *screen);

#pragma compile("section_logo.c")

#endif // SECTION_LOGO_H
