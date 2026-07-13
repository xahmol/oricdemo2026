// section_macaw_showcase.c - see section_macaw_showcase.h.
//
// assets/macaw.bin: a scarlet macaw head study (Jurong Bird Park, CC BY
// 2.0, Kandukuru Nagarjun -- commons.wikimedia.org), converted with
// tools/oric_pictconv.py's `pictoric` mode (--no-inverse-attr) -- see
// docs/pictconv.md for the algorithm and its verification against the
// real upstream PictOric.lua tool. The first real photograph tried this
// project's own image-conversion search, picked for its large, bold
// blocks of saturated colour (red/orange/white/black) rather than subtle
// continuous gradients -- exactly the kind of source content this
// project's HIRES conversion modes handle best.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "scroller.h"
#include "handwriting_font.h"
#include "section_macaw_showcase.h"

#ifdef STORAGE_FLOPPY
#define MACAW_FILE 7
#else
#define MACAW_FILE "macaw.bin"
#endif

#define TAGLINE_Y 170u
#define TAGLINE   "Scarlet Macaw -- full colour on 1MHz hardware! (photo: Kandukuru Nagarjun, CC BY 2.0)...."

void section_macaw_showcase_init(const HiresBitmap *screen)
{
    picture_load(MACAW_FILE, (void *)HIRESVRAM, 8000);

    scroller_init(screen, &handwriting_font, TAGLINE, TAGLINE_Y, SCROLLER_BOUNCE);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): loops the caption scroll
// indefinitely, paced externally by main.c's own min_ticks/max_ticks
// (same convention as section_scroll_showcase.c/section_logo.c).
void section_macaw_showcase_tick(const HiresBitmap *screen)
{
    if (scroller_tick(screen))
        scroller_init(screen, &handwriting_font, TAGLINE, TAGLINE_Y, SCROLLER_BOUNCE);
}
