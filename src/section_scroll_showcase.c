// section_scroll_showcase.c - see section_scroll_showcase.h.
//
// assets/oricatmos.bin: a desk-scene silhouette (monitor + keyboard +
// tape deck), originally created for this project (not adapted from any
// external source) -- generated via a one-off Python/PIL script (clean
// vector rectangles/ellipses, no gradients/fine detail, since mono
// conversion needs high-contrast blocky source art) and converted with
// tools/oric_pictconv.py --mode mono --dither none. Loaded at runtime via
// include/picture.h, same convention as section_logo.c's oriclogo.bin and
// section_sprite_showcase.c's starfield.bin -- no separate
// hires_row_colors() baseline needed, since the picture's own conversion
// used the default white-ink/black-paper (--ink 7 --paper 0).
//
// The keyboard's two accent bars (columns 6 and 29, matching the real
// Oric Atmos keyboard's red side keys) are recoloured to Oric red via
// hires_put_ink() AFTER the picture loads -- the same ink-bracket
// technique section_background.c's draw_tree_ink()/section_bird.c's
// HxsprColor already use: an ink-only control byte placed at a genuinely
// BLANK column immediately before the accent bar's own content column
// (setting ink=red), and another immediately after (resetting ink=white
// for the rest of the row). The picture's own Python/PIL generator
// (see tools/ in git history for the one-off script) deliberately leaves
// columns 5/7/28/30 (x=30-35/42-47/168-173/180-185) completely blank for
// the keyboard's own row range specifically so these bracket writes never
// collide with real pixel content -- the same "ink-bracket-vs-content-
// column" hazard this project has hit before (see section_background.c's
// own header comment).

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "scroller.h"
#include "handwriting_font.h"
#include "section_scroll_showcase.h"

#ifdef STORAGE_FLOPPY
#define ORICATMOS_FILE 5
#else
#define ORICATMOS_FILE "oricatmos.bin"
#endif

#define TAGLINE_Y 170u
#define TAGLINE   "Oric Atmos -- 8-bit dreams since 1983...."

#define KB_Y0            160u
#define KB_Y1            185u
#define ACCENT_BAR1_COL    6u   // left accent bar's own content column
#define ACCENT_BAR2_COL   29u   // right accent bar's own content column

static void colour_keyboard_accents(void)
{
    uint8_t y;
    for (y = KB_Y0; y <= KB_Y1; y++)
    {
        hires_put_ink((uint8_t)(ACCENT_BAR1_COL - 1), y, A_FWRED);
        hires_put_ink((uint8_t)(ACCENT_BAR1_COL + 1), y, A_FWWHITE);
        hires_put_ink((uint8_t)(ACCENT_BAR2_COL - 1), y, A_FWRED);
        hires_put_ink((uint8_t)(ACCENT_BAR2_COL + 1), y, A_FWWHITE);
    }
}

void section_scroll_showcase_init(const HiresBitmap *screen)
{
    picture_load(ORICATMOS_FILE, (void *)HIRESVRAM, 8000);
    colour_keyboard_accents();

    scroller_init(screen, &handwriting_font, TAGLINE, TAGLINE_Y, SCROLLER_BOUNCE);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): loops the tagline scroll
// indefinitely, paced externally by main.c's own min_ticks/max_ticks
// (same convention as section_logo.c's circling raster bars).
void section_scroll_showcase_tick(const HiresBitmap *screen)
{
    if (scroller_tick(screen))
        scroller_init(screen, &handwriting_font, TAGLINE, TAGLINE_Y, SCROLLER_BOUNCE);
}
