// section_credits.c - see section_credits.h.
//
// assets/sunset.bin: "Sunset reflection with clouds" by CupcakePerson13
// (commons.wikimedia.org/wiki/File:Sunset_reflection_with_clouds.jpg),
// CC BY-SA 4.0, converted with tools/oric_pictconv.py --mode aic
// --dither ordered -- picked (over several other CC-licensed sunset/
// landscape photos tried) for its large, clean tree-silhouette/sky/water-
// reflection composition, which held up far better under 1-bit ordered
// dithering than busier options (fine sand/water-ripple texture in other
// candidates dithered into noisy static at this resolution). Matches the
// original "sunset over the creek" credits backdrop from this project's
// own planning notes, using a real converted photo rather than hand-drawn
// art per explicit user direction for this section.
//
// TAGLINE_Y/CAPTION_BAND_*: assets/sunset.bin is letterboxed by
// tools/oric_pictconv.py's load_and_fit() (240x200, aspect-preserving) --
// a direct byte scan found real picture content only spans roughly
// y=33-159; y=0-32 and y=160-199 are genuinely blank (uniform black
// paper, zero pixel bits set). The scroller band sits well inside the
// bottom blank margin, same "picture's own real blank margin, not a
// guess" diligence as section_macaw_showcase.c/section_scroll_showcase.c
// (both fixed a real bug earlier this project for placing a scroller
// band over busy picture content instead).

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
