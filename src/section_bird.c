// section_bird.c - animated walking-bird demo section (HIRES mode)
//
// A nod to the animated bird in the original "Welcome to Oric Atmos" demo
// (oric.org/software/welcome_to_oric_atmos-593.html). Frame data is
// assets/bird.h (see that file for provenance/attribution).
//
// Moves both horizontally (column-byte stepping, as before) and vertically
// (a sine wave via fixedmath.h's oric_sin() -- see bird_y_for_angle()).
//
// Drawn via sprite.h's hxspr_draw()/hxspr_erase() (HXSPR_XOR mode, no
// backup buffer at all) -- a byte-aligned sprite, much faster than
// hb_bitblit's per-pixel save-under HiresSprite for a sprite this size
// (see sprite.h's own comment for why). Movement is constrained to whole
// column-bytes (6px/step) to stay byte-aligned, same granularity the
// reference's own direct-poke approach uses, for the same reason.
//
// HXSPR_XOR, not HXSPR_OR/OR_SPARSE: an earlier version of this file flew
// a much taller sine wave (amplitude 35) that dipped low enough to cross
// section_background.c's own tree ink-brackets and pixel content -- XOR's
// unconditional `|0x40` corrupts an attribute byte it touches (permanently
// reclassifying it as pixel data), and XOR's own "must exactly undo the
// last draw" assumption breaks if anything else (e.g. a cloud scroll)
// touches the sprite's footprint between draw and erase. Both were real,
// confirmed bugs, fixed at the time by switching to HXSPR_OR_SPARSE with a
// byte-exact backup/restore buffer and a per-column tree-overlap "hole"
// mask (see git history/project memory for the full story).
//
// BIRD_BASE_Y/BIRD_AMPLITUDE now keep the ENTIRE sprite -- body and
// colour brackets both -- confined to the plain sky band between
// section_clouds.c's own cloud rows (14-33) and section_background.c's
// tree canopies (151-171): pure blank PAPER with no real pixel/attribute
// content ever underneath, for the sprite's whole range of travel. That
// makes the tree/cloud-collision hazards above structurally impossible
// (not just handled), so the byte-exact backup machinery they required is
// gone too -- XOR is self-inverting and needs no backup buffer at all
// (sprite.h: "pass NULL"), and the colour bracket's own restore falls back
// to sprite.c's hardcoded-blank behaviour (also correct here, since blank
// pixel data IS what's really under both bracket columns -- see
// HxsprColor's own doc comment on when NULL is safe). Net effect: roughly
// half the byte-touching work per tick compared to the OR_SPARSE version
// (no backup read on draw, no backup write on erase), on top of no longer
// needing the tree-overlap scan at all. The cost: a smaller vertical bob
// (AMPLITUDE 23 vs the original 35) -- the bird no longer dips down toward
// the treetops.
//
// Coloured black via HxsprColor's ink-only bracket (silhouette-style,
// readable against the sky's cyan paper -- yellow was tried first but was
// hard to see against cyan): assumes main.c/section_background.c have
// established a white-ink baseline for the whole screen (see
// docs/sprite.md's "Colour" section) -- the bracket's restore_ink
// (A_FWWHITE) only makes sense against that known baseline. This costs 2
// extra column-bytes (one each side of the 11-byte image), hence
// BIRD_FRAME_W_BYTES=11 but BIRD_MIN_COL/MAX_COL leave col-1/col+11 free
// rather than spanning the full 0..HIRES_ROW_BYTES-BIRD_FRAME_W_BYTES
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
// Vertical position flies in a sine wave (see bird_y_for_angle() below).
// BASE_Y/AMPLITUDE keep the ENTIRE sprite -- body and colour brackets both
// -- inside the clear sky band between section_clouds.c's own cloud rows
// (14-33) and section_background.c's tree canopies (151-171) -- see this
// file's own header comment for why that confinement matters, not just
// where the numbers come from:
//   - top edge (BASE_Y-AMPLITUDE = 38) stays 5 rows clear of the clouds'
//     own bottom edge (row 33).
//   - bottom edge (BASE_Y+AMPLITUDE+BIRD_FRAME_H-1 = 84+63 = 147) stays
//     4 rows clear of the trees' own top edge (row 151).
#define BIRD_BASE_Y         61u
#define BIRD_AMPLITUDE      23
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

// Paces the bird's OWN movement/redraw independently of main.c's own
// frame-pacing cadence (see that file) -- the same "every N main-loop
// ticks" convention section_clouds.c/section_background.c already use for
// their own scroll steps.
#define BIRD_TICK_EVERY 2u

static uint8_t bird_tick_count = 0;

static uint8_t bird_frame = 0;
static uint8_t bird_col = BIRD_MIN_COL;
static uint8_t bird_angle = 0;
static uint8_t bird_y;

// Maps a 0-255 angle to a screen row: BASE_Y +/- AMPLITUDE, via
// fixedmath.h's oric_sin() (range -127..127). Integer math only (NOFLOAT).
static uint8_t bird_y_for_angle(uint8_t angle)
{
    int16_t offset = ((int16_t)oric_sin(angle) * BIRD_AMPLITUDE) / 127;
    return (uint8_t)(BIRD_BASE_Y + offset);
}

static const HxsprColor bird_color = { true, A_FWBLACK, A_FWWHITE };

void section_bird_init(const HiresBitmap *screen)
{
    bird_y = bird_y_for_angle(bird_angle);
    hxspr_draw(screen, bird_walk, BIRD_FRAME_W_BYTES, BIRD_FRAME_H, bird_col, bird_y,
               HXSPR_XOR, (uint8_t *)0, &bird_color, (uint8_t *)0);
}

void section_bird_tick(const HiresBitmap *screen)
{
    const uint8_t *frame;

    bird_tick_count++;
    if (bird_tick_count < BIRD_TICK_EVERY)
        return;
    bird_tick_count = 0;

    // Erase: re-XOR the SAME image at the SAME (col, y) currently drawn
    // there (sprite.h's own requirement for HXSPR_XOR) -- bird_frame/
    // bird_col/bird_y still hold the values from the LAST draw at this
    // point, before any of them advance below.
    frame = bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE;
    hxspr_erase(screen, frame, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
                bird_col, bird_y, HXSPR_XOR, (uint8_t *)0, &bird_color, (uint8_t *)0);

    bird_frame++;
    if (bird_frame >= BIRD_FRAME_COUNT)
        bird_frame = 0;
    bird_col++;
    if (bird_col > BIRD_MAX_COL)
        bird_col = BIRD_MIN_COL;
    bird_angle = (uint8_t)(bird_angle + BIRD_ANGLE_STEP);
    bird_y = bird_y_for_angle(bird_angle);

    frame = bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE;
    hxspr_draw(screen, frame, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
               bird_col, bird_y, HXSPR_XOR, (uint8_t *)0, &bird_color, (uint8_t *)0);
}
