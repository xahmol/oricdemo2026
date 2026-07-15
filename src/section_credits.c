// section_credits.c - see section_credits.h.
//
// assets/sunset.bin: "Silhouette of palm trees at tropical sunset on Bali
// island" by Artem Beliaikin (Moscow, Russia)
// (commons.wikimedia.org/wiki/File:Silhouette_of_palm_trees_at_tropical_sunset_on_Bali_island._(44817340244).jpg),
// CC0 1.0 Universal Public Domain Dedication, converted with
// tools/oric_pictconv.py --mode pictoric --dither ordered -- the SAME
// technique section_macaw_showcase.c's own photo already uses for real
// multi-colour output (genuine reds/cyans/magentas/greens across the sky
// gradient and palm silhouettes), picked after an earlier `aic`-mode
// sunset/water-reflection photo read as too monochrome (only ever
// alternates between 2 fixed hues, one per row parity) per explicit user
// feedback wanting "a more colourful" backdrop -- `colored` mode was also
// tried on both candidate photos and produced broken/garbled output for
// this kind of continuous-gradient sky content (works far better on bold
// solid-colour-block sources, per macaw.bin's own header comment); only
// `pictoric` gave genuinely rich colour here.
//
// TAGLINE_Y/CAPTION_BAND_*: assets/sunset.bin is letterboxed by
// tools/oric_pictconv.py's load_and_fit() (240x200, aspect-preserving) --
// a direct byte scan found real picture content spans roughly y=0-180;
// y=182-199 is genuinely blank (uniform 0x40 bytes). The scroller band
// sits inside that bottom blank margin, same "picture's own real blank
// margin, not a guess" diligence as section_macaw_showcase.c/
// section_scroll_showcase.c (both fixed a real bug earlier this project
// for placing a scroller band over busy picture content instead).

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "scroller.h"
#include "strings.h"
#include "section_credits.h"
#include "section_common.h"

#ifdef STORAGE_FLOPPY
#define SUNSET_FILE 8
#else
#define SUNSET_FILE "sunset.bin"
#endif

#define TAGLINE_Y 185u

#define CAPTION_BAND_Y0 182u
#define CAPTION_BAND_Y1 199u

// Chained through by index -- see section_credits.h's own header comment
// for why this reuses scroller.c/h completely unchanged (one string at a
// time) rather than extending that module for multi-line input.
static const char *const credit_lines[] = {
    MSG_CREDIT_TITLE,
    MSG_CREDIT_HOMAGE,
    MSG_CREDIT_AUTHOR,
    MSG_CREDIT_REPO,
    MSG_CREDIT_TOOLS,
    MSG_CREDIT_BRAND,
    MSG_CREDIT_MUSIC1,
    MSG_CREDIT_MUSIC2,
    MSG_CREDIT_BIRD,
    MSG_CREDIT_LOGO,
    MSG_CREDIT_SUNSET,
    MSG_CREDIT_THANKS,
};
#define NUM_CREDIT_LINES (sizeof(credit_lines) / sizeof(credit_lines[0]))

static uint8_t credit_index;

static void clear_caption_band(const HiresBitmap *screen)
{
    uint8_t y;
    hb_rect_fill(screen, (const HiresClip *)0, 0, CAPTION_BAND_Y0, HIRES_WIDTH_PX,
                 (uint8_t)(CAPTION_BAND_Y1 - CAPTION_BAND_Y0 + 1), false);
    for (y = CAPTION_BAND_Y0; y <= CAPTION_BAND_Y1; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);
}

void section_credits_init(const HiresBitmap *screen)
{
    picture_load(SUNSET_FILE, (void *)HIRESVRAM, 8000);
    clear_caption_band(screen);

    credit_index = 0;
    scroller_init(screen, credit_lines[credit_index], TAGLINE_Y, SCROLLER_PLAIN);
}

// void, not bool -- see section_common.h's own header comment for why.
// Calls section_mark_finished() once every credit_lines[] entry has
// scrolled fully off screen -- a real natural end, like
// section_dissolve_showcase.c's own reveal-completion.
void section_credits_tick(const HiresBitmap *screen)
{
    if (scroller_tick(screen))
    {
        credit_index++;
        if (credit_index >= NUM_CREDIT_LINES)
        {
            section_mark_finished();
            return;
        }
        scroller_init(screen, credit_lines[credit_index], TAGLINE_Y, SCROLLER_PLAIN);
    }
}
