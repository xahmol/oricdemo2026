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
#define SAT_Y       84u
#define MOVE_EVERY_TICKS 3u

static uint8_t sat_col;
static uint8_t move_wait;
static uint8_t sat_backup[SATELLITE_W_BYTES * SATELLITE_H];

void section_sprite_showcase_init(const HiresBitmap *screen)
{
    // picture_load() writes straight into HIRESVRAM (screen->data IS
    // HIRESVRAM for the live canvas) -- see section_logo.c's own comment
    // on this same pattern. Silently leaves the previous section's own
    // final frame on screen if this fails (no LOCI/floppy device, file
    // missing), same graceful-failure posture as arkos_load().
    picture_load(STARFIELD_FILE, (void *)HIRESVRAM, 8000);

    sat_col   = SAT_MIN_COL;
    move_wait = 0;

    hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
               sat_col, SAT_Y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
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
                sat_col, SAT_Y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);

    sat_col++;
    if (sat_col > SAT_MAX_COL)
        sat_col = SAT_MIN_COL;

    hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
               sat_col, SAT_Y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
}
