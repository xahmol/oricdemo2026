// section_background.h - static sky + creek background (HIRES mode)

#ifndef SECTION_BACKGROUND_H
#define SECTION_BACKGROUND_H

#include "hires.h"

// Draws the static sky + creek background once. Call before any section
// that draws on top of it (e.g. section_bird_run()).
void section_background_run(const HiresBitmap *screen);

#pragma compile("section_background.c")

#endif // SECTION_BACKGROUND_H
