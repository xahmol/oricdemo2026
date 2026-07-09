// section_bird.c - animated walking-bird demo section (HIRES mode)
//
// A nod to the animated bird in the original "Welcome to Oric Atmos" demo
// (oric.org/software/welcome_to_oric_atmos-593.html). Frame data is
// assets/bird.h (see that file for provenance/attribution).
//
// Moves both horizontally (column-byte stepping, as before) and vertically
// (a sine wave via fixedmath.h's oric_sin() -- see bird_y_for_angle()).
//
// Drawn via sprite.h's hxspr_draw()/hxspr_erase() (HXSPR_XOR mode) -- a
// byte-aligned sprite, much faster than hb_bitblit's per-pixel save-under
// HiresSprite for a sprite this size (see sprite.h's own comment for why).
// Movement is constrained to whole column-bytes (6px/step) to stay
// byte-aligned, same granularity the reference's own direct-poke approach
// uses, for the same reason.
//
// Coloured black via HxsprColor's ink-only bracket (silhouette-style,
// readable against the sky's cyan/the creek's blue paper -- yellow was
// tried first but was hard to see against cyan): assumes main.c/
// section_background.c have established a white-ink baseline for the
// whole screen (see docs/sprite.md's "Colour" section) -- the bracket's
// restore_ink (A_FWWHITE) only makes sense against that known baseline.
// This costs 2 extra column-bytes (one each side of the 11-byte image),
// hence BIRD_FRAME_W_BYTES=11 but BIRD_MIN_COL/MAX_COL leave col-1/col+11
// free rather than spanning the full 0..HIRES_ROW_BYTES-BIRD_FRAME_W_BYTES
// range.

#include "oric.h"
#include "hires.h"
#include "sprite.h"
#include "fixedmath.h"
#include "section_bird.h"
#include "bird.h"

#define BIRD_FRAME_W_BYTES  11u          // 66px / 6 -- the image itself, NOT counting colour brackets
#define BIRD_FRAME_H        64u
#define BIRD_FRAME_COUNT    7u
#define BIRD_FRAME_SIZE     (BIRD_FRAME_W_BYTES * BIRD_FRAME_H)
// Vertical position now flies in a sine wave (see bird_y_for_angle() below)
// instead of walking a fixed row. BASE_Y/AMPLITUDE are chosen to keep the
// ENTIRE sprite -- body and colour brackets both -- clear of BOTH hazards:
//   - row HIRES_ROWS-1 (199): its last column (39) is $BF3F, the footer's
//     own sticky TEXT-mode trigger byte (see hires_footer_enable() in
//     main.c) -- a past bug (see git history) proved writing there
//     corrupts the footer.
//   - section_background.c's creek band (rows 172+) -- flying UNDER it
//     is fine data-wise (XOR doesn't care about the PAPER attribute a
//     pixel byte happens to render with), but flying mostly ABOVE it is
//     the intended "bird over the creek" composition.
// BASE_Y=70, AMPLITUDE=35 keeps the lowest point's bottom edge at row 168
// (70+35+64-1), clear of the creek's row 172 top edge, and the highest
// point's top edge at row 35 (70-35), clear of the top of screen.
#define BIRD_BASE_Y         70u
#define BIRD_AMPLITUDE      35
#define BIRD_ANGLE_STEP     10u                                                 // tune for wave frequency
// BIRD_MIN_COL must keep the ENTIRE sprite -- left ink-bracket (col-1) AND
// the 11-byte body itself (col..col+10) -- clear of columns 0/1, which
// main.c's hires_row_colors() sweep uses as every row's own PERMANENT
// ink/paper baseline attribute bytes. col=1 (leaving only col-1=0 free)
// looked sufficient for the bracket alone, but the body's own first byte
// (column 1) still landed squarely on the row's PAPER attribute byte --
// XORing pixel data onto it (and later erasing) destroyed that row's
// baseline permanently, corrupting its colour state for good (a real bug:
// looked fine initially, then cascaded into visual chaos once the sprite
// had cycled back through its leftmost columns a few times). col=3 keeps
// col-1=2, clear of both baseline columns.
#define BIRD_MIN_COL        3u
#define BIRD_MAX_COL        (HIRES_ROW_BYTES - BIRD_FRAME_W_BYTES - 1u)         // leaves col+11 free too (28)
#define BIRD_STEP_DELAY     300u                                                // tune for walk speed

// Maps a 0-255 angle to a screen row: BASE_Y +/- AMPLITUDE, via
// fixedmath.h's oric_sin() (range -127..127). Integer math only (NOFLOAT).
static uint8_t bird_y_for_angle(uint8_t angle)
{
    int16_t offset = ((int16_t)oric_sin(angle) * BIRD_AMPLITUDE) / 127;
    return (uint8_t)(BIRD_BASE_Y + offset);
}

static const HxsprColor bird_color = { true, A_FWBLACK, A_FWWHITE };

// Busy-wait delay between animation ticks. Uses a genuine volatile memory
// write each iteration, not just a volatile loop counter -- Oscar64's
// optimizer will collapse the latter (see docs/keyboard.md's
// keyb_scan()-as-delay note, and src/hires_test.c's rasterirq test comment
// for the empirical writeup of this exact gotcha).
static uint8_t bird_wait_scratch;

static void bird_wait(uint16_t ticks)
{
    volatile uint8_t *scratch = (volatile uint8_t *)&bird_wait_scratch;
    uint16_t i;
    for (i = 0; i < ticks; i++)
        *scratch = (uint8_t)i;
}

static uint8_t bird_frame = 0;
static uint8_t bird_col = BIRD_MIN_COL;
static uint8_t bird_angle = 0;
static uint8_t bird_y;

void section_bird_init(const HiresBitmap *screen)
{
    bird_y = bird_y_for_angle(bird_angle);
    hxspr_draw(screen, bird_walk, BIRD_FRAME_W_BYTES, BIRD_FRAME_H, bird_col, bird_y,
               HXSPR_XOR, (uint8_t *)0, &bird_color);
}

void section_bird_tick(const HiresBitmap *screen)
{
    bird_wait(BIRD_STEP_DELAY);

    hxspr_erase(screen, bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
                bird_col, bird_y, HXSPR_XOR, (uint8_t *)0, &bird_color);

    bird_frame++;
    if (bird_frame >= BIRD_FRAME_COUNT)
        bird_frame = 0;
    bird_col++;
    if (bird_col > BIRD_MAX_COL)
        bird_col = BIRD_MIN_COL;
    bird_angle = (uint8_t)(bird_angle + BIRD_ANGLE_STEP);
    bird_y = bird_y_for_angle(bird_angle);

    hxspr_draw(screen, bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
               bird_col, bird_y, HXSPR_XOR, (uint8_t *)0, &bird_color);
}
