// section_macaw_showcase.h - a second scroller.h demonstration
//
// A caption scrolls right-to-left over a scarlet macaw photograph
// (assets/macaw.bin), a static backdrop loaded at runtime via
// include/picture.h -- reuses include/scroller.h exactly as
// section_scroll_showcase.c already does with assets/oricatmos.bin, just
// with different backdrop/text.

#ifndef SECTION_MACAW_SHOWCASE_H
#define SECTION_MACAW_SHOWCASE_H

#include <stdbool.h>
#include "hires.h"

void section_macaw_showcase_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_macaw_showcase_tick(const HiresBitmap *screen);

#pragma compile("section_macaw_showcase.c")

#endif // SECTION_MACAW_SHOWCASE_H
