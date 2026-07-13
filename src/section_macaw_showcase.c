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
#include "section_macaw_showcase.h"

#ifdef STORAGE_FLOPPY
#define MACAW_FILE 7
#else
#define MACAW_FILE "macaw.bin"
#endif

// TAGLINE_Y/SCROLLER_PLAIN: this used to sit at y=170 with
// SCROLLER_BOUNCE, squarely inside the photo's own real pixel content --
// a direct byte-level scan of assets/macaw.bin found the bird fills
// almost the ENTIRE frame (only y=0-10 and y=191-199 are genuinely
// blank), so scroller.c's own erase step (which just blanks pixels
// rather than restoring the photo) was permanently destroying part of
// the macaw's own portrait as the caption scrolled across it -- see
// section_scroll_showcase.c's own header comment for the twin bug this
// was first found in (that one on the keyboard illustration, this one
// on the macaw photo). Moved to the picture's own blank bottom margin
// (y=191, SCROLLER_GLYPH_H=8 tall) with the bounce style dropped (no
// headroom for +/-6px of wobble in only a 9-row margin).
// CAPTION_BAND_* below is also cleared explicitly on top of that.
#define TAGLINE_Y 191u
#define TAGLINE   "Scarlet Macaw -- full colour on 1MHz hardware! (photo: Kandukuru Nagarjun, CC BY 2.0)...."

#define CAPTION_BAND_Y0 188u
#define CAPTION_BAND_Y1 199u

static void clear_caption_band(const HiresBitmap *screen)
{
    uint8_t y;
    hb_rect_fill(screen, (const HiresClip *)0, 0, CAPTION_BAND_Y0, HIRES_WIDTH_PX,
                 (uint8_t)(CAPTION_BAND_Y1 - CAPTION_BAND_Y0 + 1), false);
    for (y = CAPTION_BAND_Y0; y <= CAPTION_BAND_Y1; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);
}

void section_macaw_showcase_init(const HiresBitmap *screen)
{
    picture_load(MACAW_FILE, (void *)HIRESVRAM, 8000);
    clear_caption_band(screen);

    scroller_init(screen, TAGLINE, TAGLINE_Y, SCROLLER_PLAIN);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): loops the caption scroll
// indefinitely, paced externally by main.c's own min_ticks/max_ticks
// (same convention as section_scroll_showcase.c/section_logo.c).
void section_macaw_showcase_tick(const HiresBitmap *screen)
{
    if (scroller_tick(screen))
        scroller_init(screen, TAGLINE, TAGLINE_Y, SCROLLER_PLAIN);
}
