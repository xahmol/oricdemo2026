// section_sprite_showcase.c - see section_sprite_showcase.h.
//
// assets/starfield.bin: a sparse scattered-dot night sky, originally
// created for this project (not adapted from any external source) --
// generated via a one-off Python/PIL script (random single-pixel + a few
// 2x2 "brighter cluster" dots on black) and converted with
// tools/oric_pictconv.py --mode mono --dither none (source is already
// pure black/white, so no dithering needed). Loaded at runtime via
// include/picture.h, same convention as section_logo.c's oriclogo.bin --
// no separate hires_row_colors() baseline needed, since the picture's own
// conversion already used the default white-ink/black-paper (--ink 7
// --paper 0), matching this project's own whole-screen convention, and
// picture_load() overwrites the full 8000-byte frame (including
// column-bytes 0-1) with the picture's own converted pixel data.
//
// assets/satellite.h: a small original "satellite" sprite (see that
// file's own header comment) drawn via sprite.h's hxspr_draw()/
// hxspr_erase() in HXSPR_OR mode -- demonstrates the byte-exact
// backup/restore path over REAL background content (the starfield),
// the exact scenario section_bird.c no longer exercises now that it
// flies only over blank sky.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "sprite.h"
#include "satellite.h"
#include "fixedmath.h"
#include "section_sprite_showcase.h"

#ifdef STORAGE_FLOPPY
#define STARFIELD_FILE 4
#else
#define STARFIELD_FILE "starfield.bin"
#endif

// Column-byte range the sprite sweeps: leaves column-bytes 0-1 free (the
// row's own baseline ink/paper attribute bytes -- see hires.h) at the left,
// and stays within HIRES_ROW_BYTES at the right.
#define SAT_MIN_COL 2u
#define SAT_MAX_COL (HIRES_ROW_BYTES - SATELLITE_W_BYTES)
#define MOVE_EVERY_TICKS 3u

// Vertical drift, added per user feedback ("make it also move vertically")
// -- a slow oric_sin() bob around a fixed baseline, same technique
// section_bird.c's own vertical movement uses. SAT_Y_RANGE is
// DELIBERATELY a plain signed int, not "50u": mixing an unsigned literal
// into an expression with oric_sin()'s own SIGNED result is the exact
// bug this project has hit (and fixed) twice already, in
// section_wave_showcase.c's MAX_AMPLITUDE and scroller.c's
// BOUNCE_AMPLITUDE -- see either file's own header comment for the full
// mechanism. SAT_Y_BASE/SAT_Y_RANGE are chosen to keep the sprite's own
// SATELLITE_H=16px body fully on screen (base 100, +/-50px -> 50..150,
// well clear of both the top few rows and the bottom footer band).
#define SAT_Y_BASE    100u
#define SAT_Y_RANGE    50    // plain signed -- see comment above
#define SAT_Y_ANGLE_STEP 3u  // oric_sin() angle units per move-step

static uint8_t sat_col;
static uint8_t sat_y;
static uint8_t sat_angle;
static uint8_t move_wait;
static uint8_t sat_backup[SATELLITE_W_BYTES * SATELLITE_H];

static uint8_t sat_y_for_angle(uint8_t angle)
{
    int16_t offset = (int16_t)((int16_t)oric_sin(angle) * SAT_Y_RANGE / 127);
    return (uint8_t)((int16_t)SAT_Y_BASE + offset);
}

void section_sprite_showcase_init(const HiresBitmap *screen)
{
    // picture_load() writes straight into HIRESVRAM (screen->data IS
    // HIRESVRAM for the live canvas) -- see section_logo.c's own comment
    // on this same pattern. Silently leaves the previous section's own
    // final frame on screen if this fails (no LOCI/floppy device, file
    // missing), same graceful-failure posture as arkos_load().
    picture_load(STARFIELD_FILE, (void *)HIRESVRAM, 8000);

    sat_col   = SAT_MIN_COL;
    sat_angle = 0;
    sat_y     = sat_y_for_angle(sat_angle);
    move_wait = 0;

    hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
               sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): drifts indefinitely, paced
// externally by main.c's own min_ticks/max_ticks.
void section_sprite_showcase_tick(const HiresBitmap *screen)
{
    move_wait++;
    if (move_wait < MOVE_EVERY_TICKS)
        return;
    move_wait = 0;

    hxspr_erase(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
                sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);

    sat_col++;
    if (sat_col > SAT_MAX_COL)
        sat_col = SAT_MIN_COL;

    sat_angle = (uint8_t)(sat_angle + SAT_Y_ANGLE_STEP);
    sat_y = sat_y_for_angle(sat_angle);

    hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
               sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
}
