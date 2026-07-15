// section_credits.h - Phase 9's closing credits scroller
//
// Reuses include/scroller.h's engine UNCHANGED (still one string at a
// time) -- section_credits_tick() just re-calls scroller_init() with the
// next line once scroller_tick() returns true, chaining through
// strings_en.h's MSG_CREDIT_* lines until they're exhausted, then calls
// section_mark_finished() (a real natural end, like
// section_dissolve_showcase.c's own reveal-completion). Backdrop:
// assets/sunset.bin (see section_credits.c's own header comment for its
// sourcing/attribution). This is the LAST slot in main.c's sections[]
// table -- that array's own outer for(;;) loop already cycles back to
// the idi8b splash once this section finishes, so no special
// keyb_getch()-blocking "press key to exit" behaviour is needed.

#ifndef SECTION_CREDITS_H
#define SECTION_CREDITS_H

#include "hires.h"

void section_credits_init(const HiresBitmap *screen);
// void, not bool -- see section_common.h's own header comment for why.
void section_credits_tick(const HiresBitmap *screen);

#pragma compile("section_credits.c")

#endif // SECTION_CREDITS_H
