// section_sprite_showcase.c - see section_sprite_showcase.h.
//
// The starfield used to be assets/starfield.bin, a converted picture
// (originally created for this project, a Python/PIL-generated scatter
// of dots) -- replaced with PROCEDURAL generation per explicit user
// feedback ("not really necessary for just random dots"). NUM_STARS
// (x,y) positions are precomputed (Python's random module, a fixed seed
// for a reproducible scatter -- see the values below) rather than
// computed at runtime: this project's own established preference for
// baking in one-off "looks right, never needs recomputing" numbers
// instead of pulling in RNG/trig code for something that doesn't
// genuinely need it at runtime (same reasoning as
// section_rasterirq_showcase.c's own precomputed star_dx[]/star_dy[]).
//
// Each star only ever moves LEFT, wrapping to the right edge at its own
// fixed row -- a classic side-scrolling starfield, not a randomized
// respawn-anywhere effect. Deliberately slower than the satellite's own
// movement (STAR_MOVE_EVERY_TICKS > MOVE_EVERY_TICKS): a distant
// background layer should read as slower than a nearer foreground
// object, the same two-speed depth convention section_clouds.c's own
// header comment already establishes for the bird scene.
//
// assets/satellite.h: a small original "satellite" sprite (see that
// file's own header comment) drawn via sprite.h's hxspr_draw()/
// hxspr_erase() in HXSPR_OR mode -- demonstrates the byte-exact
// backup/restore path over REAL background content (now the
// procedural starfield, previously the picture).
//
// Ordering in tick() matters: satellite erase, THEN star movement, THEN
// satellite move+redraw. A star's own hb_clr()/hb_set() could in
// principle touch a pixel currently occupied by the satellite sprite
// (both are independent per-pixel writers sharing the same canvas) --
// running star movement while the satellite is genuinely absent from
// the screen (between its own erase and redraw) avoids that interaction
// entirely, rather than trying to detect/avoid overlap explicitly.

#include "oric.h"
#include "hires.h"
#include "sprite.h"
#include "satellite.h"
#include "fixedmath.h"
#include "section_sprite_showcase.h"

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

#define NUM_STARS 40u
#define STAR_MIN_X 12u   // matches SAT_MIN_COL*6 -- reserved column-bytes 0-1
// Slower than MOVE_EVERY_TICKS above -- see this file's own header
// comment for why (distant background vs. nearer foreground).
#define STAR_MOVE_EVERY_TICKS 4u

// Precomputed scatter (Python: random.seed(42); random.randint(12,239)
// for x, random.randint(0,199) for y -- x already clear of the reserved
// column-bytes 0-1 margin). Each star keeps its own y forever; only x
// changes at runtime (see star_step() below).
static const uint8_t star_init_x[NUM_STARS] = {
    175, 40, 18, 201, 82, 74, 69, 47, 200, 38, 185, 201, 151, 34, 163, 120,
    20, 19, 35, 67, 71, 141, 166, 18, 155, 62, 195, 178, 191, 151, 119, 68,
    126, 162, 83, 219, 234, 13, 206, 218
};
static const uint8_t star_init_y[NUM_STARS] = {
    40, 178, 108, 87, 71, 39, 55, 195, 86, 26, 23, 97, 24, 91, 88, 154,
    67, 11, 186, 117, 137, 31, 96, 20, 141, 75, 160, 158, 92, 147, 49, 180,
    17, 11, 169, 58, 197, 74, 20, 59
};

static uint8_t star_x[NUM_STARS];

static uint8_t sat_col;
static uint8_t sat_y;
static uint8_t sat_angle;
static uint8_t move_wait;
static uint8_t star_move_wait;
static uint8_t sat_backup[SATELLITE_W_BYTES * SATELLITE_H];

static uint8_t sat_y_for_angle(uint8_t angle)
{
    int16_t offset = (int16_t)((int16_t)oric_sin(angle) * SAT_Y_RANGE / 127);
    return (uint8_t)((int16_t)SAT_Y_BASE + offset);
}

// Shifts one star one pixel left, wrapping to the right edge at the same
// row when it reaches the reserved left margin.
static void star_step(const HiresBitmap *screen, uint8_t i)
{
    hb_clr(screen, star_x[i], star_init_y[i]);
    if (star_x[i] <= STAR_MIN_X)
        star_x[i] = HIRES_WIDTH_PX - 1u;
    else
        star_x[i]--;
    hb_set(screen, star_x[i], star_init_y[i]);
}

void section_sprite_showcase_init(const HiresBitmap *screen)
{
    uint8_t i;

    hb_fill(screen, 0x40);   // real RAM isn't zero-initialized -- start blank
    for (i = 0; i < HIRES_ROWS; i++)
        hires_row_colors(i, A_FWWHITE, A_BGBLACK);

    for (i = 0; i < NUM_STARS; i++)
    {
        star_x[i] = star_init_x[i];
        hb_set(screen, star_x[i], star_init_y[i]);
    }

    sat_col   = SAT_MIN_COL;
    sat_angle = 0;
    sat_y     = sat_y_for_angle(sat_angle);
    move_wait = 0;
    star_move_wait = 0;

    hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
               sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): drifts indefinitely, paced
// externally by main.c's own min_ticks/max_ticks.
void section_sprite_showcase_tick(const HiresBitmap *screen)
{
    bool move_sat, move_stars;

    move_wait++;
    move_sat = (move_wait >= MOVE_EVERY_TICKS);
    if (move_sat)
        move_wait = 0;

    star_move_wait++;
    move_stars = (star_move_wait >= STAR_MOVE_EVERY_TICKS);
    if (move_stars)
        star_move_wait = 0;

    if (!move_sat && !move_stars)
        return;

    // See this file's own header comment for why erase-stars-redraw is
    // ordered this way (satellite genuinely absent from the screen while
    // stars move, avoiding any pixel-level interaction between the two).
    if (move_sat)
        hxspr_erase(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
                    sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);

    if (move_stars)
    {
        uint8_t i;
        for (i = 0; i < NUM_STARS; i++)
            star_step(screen, i);
    }

    if (move_sat)
    {
        sat_col++;
        if (sat_col > SAT_MAX_COL)
            sat_col = SAT_MIN_COL;

        sat_angle = (uint8_t)(sat_angle + SAT_Y_ANGLE_STEP);
        sat_y = sat_y_for_angle(sat_angle);

        hxspr_draw(screen, satellite_sprite, SATELLITE_W_BYTES, SATELLITE_H,
                   sat_col, sat_y, HXSPR_OR, sat_backup, (const HxsprColor *)0, (uint8_t *)0);
    }
}
