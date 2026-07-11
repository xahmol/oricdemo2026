// section_logo.c - see section_logo.h.
//
// assets/oriclogo.bin: the clean vector "ORIC ATMOS 48K" wordmark from
// Wikimedia Commons (commons.wikimedia.org/wiki/File:Logo_Oric_Atmos.png
// -- Oric International's own vintage company wordmark/hardware
// branding, reproduced here as a non-commercial fan homage, same
// attribution posture as assets/bird.h's own external-asset credit),
// NOT oric.org's own tape-loading screenshot (a noisier, lower-res
// scan tried first -- this source converts far more cleanly). The
// original's red diagonal slash + underline bar were recoloured to
// white before conversion (max-channel brightness threshold, hue-
// independent so the red's own high R channel still counts as "bright") --
// per the user's own explicit direction: this project's serial ink/paper
// attribute bytes can only ever hold ONE ink colour per row-span, so a
// separate red would need its own ink bracket columns the same way
// section_bird.c's letters do, adding real complexity for a static
// logo that doesn't need more than one colour. Converted via
// tools/oric_pictconv.py --mode mono --dither floyd-steinberg (forced
// white ink), full 240x200 HIRES frame (the tool's own --width/--height
// flags currently only accept the full HIRES resolution -- see that
// tool's own --help -- so the logo is resized/composited onto an
// otherwise-blank canvas at its intended screen position, rather than a
// true sub-canvas crop; harmless, since this loads straight into
// HIRESVRAM with nothing else sharing the frame). Loaded at runtime via
// include/picture.h (see that header's own comment for why: keeps this
// ~8000-byte asset out of the ~36KB code/data/BSS budget entirely, same
// reasoning as the Arkos music module).
//
// Raster bars: a single highlight band circles through the logo's own
// row range (LOGO_TOP..LOGO_BOTTOM), moving BAR_SPEED scanlines per main-
// loop tick -- sweeping down as a PAPER-attribute change (appears to sit
// "under"/behind the white logo strokes), then, on reaching the bottom,
// reversing to sweep back up as an INK-attribute change (appears to sit
// "over"/in front of the strokes, tinting them), reaching the top, and
// repeating indefinitely.
//
// Deliberately NOT built on include/rasterirq.h's hrirq_add() mid-frame
// IRQ callbacks: HIRES attribute bytes are embedded VRAM data the ULA
// reads continuously as it scans every frame, so a byte written well
// ahead of time (a plain hires_row_colors() call, same technique
// section_background.c's own colour bands use) looks IDENTICAL to one
// written via precisely-timed IRQ, as long as it holds the right value
// by the time the beam gets there -- true mid-frame IRQ timing only
// matters when the SAME byte position needs to show MULTIPLE different
// values within a single frame (impossible via a static write), which
// this effect never needs: the bar's position only changes once per
// tick, not multiple times within one 20ms frame. Genuine mid-frame
// raster-IRQ timing is reserved for the later standalone raster-IRQ
// showcase (a different section), where it actually is necessary.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "section_logo.h"

#ifdef STORAGE_FLOPPY
#define LOGO_FILE 2
#else
#define LOGO_FILE "oriclogo.bin"
#endif

// Row range the logo occupies within the loaded 240x200 picture (see this
// file's own header comment on how assets/oriclogo.bin was composited --
// resized to 210px wide, placed at x=15/y=45, so this spans y=45..154) --
// the bar circles within exactly this range.
#define LOGO_TOP     45u
#define LOGO_BOTTOM  154u
#define BAR_HEIGHT     6u
#define BAR_SPEED      2u
#define BAR_MAX_Y    (LOGO_BOTTOM - BAR_HEIGHT + 1u)

typedef enum
{
    BAR_PHASE_DOWN,   // sweeping down, paper-attribute highlight ("under")
    BAR_PHASE_UP      // sweeping up, ink-attribute highlight ("over")
} BarPhase;

static uint8_t  bar_y;
static BarPhase bar_phase;

static void set_rows(uint8_t y0, uint8_t h, uint8_t ink, uint8_t paper)
{
    uint8_t y;
    for (y = y0; y < (uint8_t)(y0 + h); y++)
        hires_row_colors(y, ink, paper);
}

void section_logo_init(const HiresBitmap *screen)
{
    (void)screen;

    // picture_load() writes straight into HIRESVRAM (screen->data IS
    // HIRESVRAM for the live canvas) -- no separate scratch buffer or
    // blit step needed, matching arkos_load()'s own "load straight into
    // its fixed destination" convention. Silently leaves the previous
    // section's own final frame on screen if this fails (no LOCI/floppy
    // device, file missing) rather than crashing, same graceful-failure
    // posture as arkos_load().
    picture_load(LOGO_FILE, (void *)HIRESVRAM, 8000);

    bar_y = LOGO_TOP;
    bar_phase = BAR_PHASE_DOWN;
    set_rows(bar_y, BAR_HEIGHT, A_FWWHITE, A_BGYELLOW);
}

bool section_logo_tick(const HiresBitmap *screen)
{
    (void)screen;

    // Restore the bar's current rows back to the picture's own baseline
    // (white ink, black paper -- the mono conversion's own colours)
    // before moving it.
    set_rows(bar_y, BAR_HEIGHT, A_FWWHITE, A_BGBLACK);

    if (bar_phase == BAR_PHASE_DOWN)
    {
        if (bar_y + BAR_SPEED > BAR_MAX_Y)
        {
            bar_y = BAR_MAX_Y;
            bar_phase = BAR_PHASE_UP;
        }
        else
            bar_y = (uint8_t)(bar_y + BAR_SPEED);
    }
    else
    {
        if (bar_y < (uint8_t)(LOGO_TOP + BAR_SPEED))
        {
            bar_y = LOGO_TOP;
            bar_phase = BAR_PHASE_DOWN;
        }
        else
            bar_y = (uint8_t)(bar_y - BAR_SPEED);
    }

    if (bar_phase == BAR_PHASE_DOWN)
        set_rows(bar_y, BAR_HEIGHT, A_FWWHITE, A_BGYELLOW);   // under
    else
        set_rows(bar_y, BAR_HEIGHT, A_FWYELLOW, A_BGBLACK);   // over

    return false;   // circles indefinitely -- no natural end
}
