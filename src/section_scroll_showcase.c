// section_scroll_showcase.c - see section_scroll_showcase.h.
//
// assets/oricatmos.bin: a desk-scene silhouette (monitor + keyboard +
// tape deck), originally created for this project (not adapted from any
// external source) -- generated via a one-off Python/PIL script (clean
// vector rectangles/ellipses, no gradients/fine detail, since mono
// conversion needs high-contrast blocky source art) and converted with
// tools/oric_pictconv.py --mode mono --dither none. Loaded at runtime via
// include/picture.h, same convention as section_logo.c's oriclogo.bin --
// no separate hires_row_colors() baseline needed, since the picture's own
// conversion used the default white-ink/black-paper (--ink 7 --paper 0).
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
#include "section_scroll_showcase.h"

#ifdef STORAGE_FLOPPY
#define ORICATMOS_FILE 4
#else
#define ORICATMOS_FILE "oricatmos.bin"
#endif

// TAGLINE_Y/SCROLLER_PLAIN: the scroller used to sit at y=170 with
// SCROLLER_BOUNCE (a +/-6px vertical wobble), which put it squarely
// inside the keyboard illustration's own real pixel content (y=160-185)
// -- since scroller.c's own erase step just blanks pixels rather than
// restoring whatever picture content was there, the keyboard artwork was
// being permanently destroyed (turned blank) as the caption scrolled
// across it, and never redrawn. Real bug, found via a direct byte-level
// scan of assets/oricatmos.bin confirming non-blank content at every
// sampled row in that range. The picture's own genuinely blank margin at
// the BOTTOM is y=187-199 -- moved the tagline there instead (y=191,
// SCROLLER_GLYPH_H=8 tall, so it occupies 191-198) and dropped the
// bounce style (no headroom in a 13-row margin for +/-6px of wobble on
// top of an 8px glyph). CAPTION_BAND_* below is also cleared explicitly
// on top of that -- belt and braces, not just relying on the picture's
// own margin happening to already be blank.
#define TAGLINE_Y 191u
#define TAGLINE   "Oric Atmos -- 8-bit dreams since 1983...."

#define CAPTION_BAND_Y0 188u
#define CAPTION_BAND_Y1 199u

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

static void clear_caption_band(const HiresBitmap *screen)
{
    uint8_t y;
    hb_rect_fill(screen, (const HiresClip *)0, 0, CAPTION_BAND_Y0, HIRES_WIDTH_PX,
                 (uint8_t)(CAPTION_BAND_Y1 - CAPTION_BAND_Y0 + 1), false);
    for (y = CAPTION_BAND_Y0; y <= CAPTION_BAND_Y1; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);
}

void section_scroll_showcase_init(const HiresBitmap *screen)
{
    picture_load(ORICATMOS_FILE, (void *)HIRESVRAM, 8000);
    colour_keyboard_accents();
    clear_caption_band(screen);

    scroller_init(screen, TAGLINE, TAGLINE_Y, SCROLLER_PLAIN);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): loops the tagline scroll
// indefinitely, paced externally by main.c's own min_ticks/max_ticks
// (same convention as section_logo.c's circling raster bars).
void section_scroll_showcase_tick(const HiresBitmap *screen)
{
    if (scroller_tick(screen))
        scroller_init(screen, TAGLINE, TAGLINE_Y, SCROLLER_PLAIN);
}
