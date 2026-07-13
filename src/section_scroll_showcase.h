// section_scroll_showcase.h - include/scroller.h showcase
//
// A short tagline scrolls right-to-left (SCROLLER_BOUNCE style) over
// "Oric Atmos computer setup" (assets/oricatmos.bin), a static backdrop
// loaded at runtime via include/picture.h. Demonstrates scroller.h, later
// reused by src/section_credits.c with different text/style.

#ifndef SECTION_SCROLL_SHOWCASE_H
#define SECTION_SCROLL_SHOWCASE_H

#include <stdbool.h>
#include "hires.h"

void section_scroll_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_scroll_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_scroll_showcase.c")

#endif // SECTION_SCROLL_SHOWCASE_H
