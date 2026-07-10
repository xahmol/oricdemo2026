// section_bird.c - animated walking-bird demo section (HIRES mode)
//
// A nod to the animated bird in the original "Welcome to Oric Atmos" demo
// (oric.org/software/welcome_to_oric_atmos-593.html). Frame data is
// assets/bird.h (see that file for provenance/attribution).
//
// Moves both horizontally (column-byte stepping, as before) and vertically
// (a sine wave via fixedmath.h's oric_sin() -- see bird_y_for_angle()).
//
// Drawn via sprite.h's hxspr_draw()/hxspr_erase() (HXSPR_OR mode, with a
// bird_body_backup save/restore buffer) -- a byte-aligned sprite, much
// faster than hb_bitblit's per-pixel save-under HiresSprite for a sprite
// this size (see sprite.h's own comment for why). Movement is constrained
// to whole column-bytes (6px/step) to stay byte-aligned, same granularity
// the reference's own direct-poke approach uses, for the same reason.
//
// HXSPR_OR, not HXSPR_XOR: this sprite's sine-wave flight dips low enough
// to cross section_background.c's tree ink-brackets (attribute bytes,
// bit6=0) with its own BODY, not just its colour bracket. XOR's `|0x40`
// forces bit6=1 on every byte it touches, on BOTH draw and erase --
// harmless for genuine pixel data (already bit6=1), but for an attribute
// byte it permanently reclassifies it as pixel data (real Oric hardware:
// (byte&0x60)==0 means "attribute", so forcing bit6=1 changes what the
// byte MEANS to the ULA, not just its value) -- confirmed as the actual
// cause of trees corrupting/disappearing once the bird's body had ever
// crossed their ink-bracket columns, a real bug independent of (and not
// fixed by) the colour-bracket save/restore fix below. HXSPR_OR sidesteps
// this entirely: hxspr_draw() backs up the REAL pre-draw bytes (whatever
// they are) into bird_body_backup, and hxspr_erase() copies them straight
// back byte-exact -- no forced-bit assumption at all, same robustness
// HxsprColor's own color_backup buffer already relies on (see sprite.h).
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
//
// bird_color_for() (below) computes the CORRECT ink/restore_ink for the
// CURRENT position before every draw/erase call, checking whether either
// bracket column exactly coincides with one of section_background.c's own
// tree ink-brackets (a separate, independent ink-bracket sharing the same
// scanlines) via section_background_tree_bracket_ink() -- so the sprite
// writes the right byte in one atomic step. An earlier version instead let
// hxspr_draw() write its own normal bird colour, then called a separate
// "fix-up" function afterward to correct any tree-bracket collision -- that
// was rejected: hxspr_draw()'s own body loop takes long enough (a few
// thousand cycles, a substantial fraction of a tick) that the fix-up left
// a real, frequently-visible window where the wrong colour was on screen,
// not a rare one-frame flicker. Computing the right colour BEFORE drawing
// has no such window. See section_background.h's own comment for this
// function's one remaining (small, cosmetic, self-correcting) residual.

#include <string.h>
#include "oric.h"
#include "hires.h"
#include "sprite.h"
#include "fixedmath.h"
#include "section_bird.h"
#include "section_background.h"
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

// HxsprColor bracket save/restore buffer (see sprite.h) -- the bird's sine
// wave dips low enough to overlap section_background.c's tree canopies, so
// the bracket columns can't assume blank background; this buffer lets
// hxspr_erase() restore the real bytes that were there instead.
static uint8_t bird_color_backup[2 * BIRD_FRAME_H];

// HXSPR_OR body save/restore buffer -- see this file's own header comment
// for why the body itself needs the same byte-exact treatment as the
// colour bracket above, not just XOR's "self-inverting" assumption.
static uint8_t bird_body_backup[BIRD_FRAME_W_BYTES * BIRD_FRAME_H];

// Per-tick modified copy of the current frame, with any column that would
// land on a tree's own ink-bracket (not just the bird's OWN 2 bracket
// columns -- the 11-column-wide BODY itself can cross one too) blanked
// out to 0 -- see bird_prepare_frame()'s own comment.
static uint8_t bird_frame_scratch[BIRD_FRAME_W_BYTES * BIRD_FRAME_H];

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

// Computes the bird's colour bracket for a draw at (col, y): normally
// identical to bird_color, but substitutes a tree's own ink value at
// either bracket column that exactly coincides with one of
// section_background.c's own tree ink-brackets -- see this file's own
// header comment and section_background.h's for the full story. Only
// needed before DRAW: hxspr_erase() restores bracket columns byte-exact
// from color_backup regardless of the ink/restore_ink values passed to
// it (see sprite.c), so erase calls keep using the plain bird_color.
static HxsprColor bird_color_for(uint8_t col, uint8_t y)
{
    HxsprColor c = bird_color;
    uint8_t left = (uint8_t)(col - 1);
    uint8_t right = (uint8_t)(col + BIRD_FRAME_W_BYTES);
    uint8_t row_max = (uint8_t)(y + BIRD_FRAME_H - 1);
    uint8_t ink;

    if (section_background_tree_bracket_ink(left, y, row_max, &ink))
        c.ink = ink;
    if (section_background_tree_bracket_ink(right, y, row_max, &ink))
        c.restore_ink = ink;
    return c;
}

// Copies `src` (one packed BIRD_FRAME_W_BYTES*BIRD_FRAME_H frame) into
// bird_frame_scratch, marking (with the 0xFF sentinel -- see sprite.h's
// HXSPR_OR_SPARSE) any of the 11 body columns that would otherwise land
// on one of section_background.c's own tree ink-brackets for some row
// this sprite spans at `y` as a "hole" -- these are ATTRIBUTE bytes (see
// docs/sprite.md), and plain HXSPR_OR's OR-compose forces bit6=1 on every
// body byte regardless of the image bit, so even an all-zero image byte
// would still corrupt an attribute byte it touches (a real, confirmed
// bug: the bird's body flying directly over a tree's bracket column --
// not just the bird's OWN 2 bracket columns, which bird_color_for() above
// already handles -- showed garbled colour for as many ticks as the
// body's 11-column width kept overlapping that one column). Marked
// columns are skipped ENTIRELY by HXSPR_OR_SPARSE (no read/write/backup),
// at the cost of a narrow "hole" in the bird's own silhouette there.
//
// Deterministic in (src, col, y): calling this again with the SAME
// arguments (as hxspr_erase() needs -- "the SAME image currently drawn
// there") reproduces the identical masked bytes, so no state needs to
// persist between a draw and its matching erase beyond col/y/frame
// (already tracked in bird_col/bird_y/bird_frame).
static const uint8_t *bird_prepare_frame(const uint8_t *src, uint8_t col, uint8_t y)
{
    uint8_t idx;
    uint8_t row_max = (uint8_t)(y + BIRD_FRAME_H - 1);
    memcpy(bird_frame_scratch, src, sizeof(bird_frame_scratch));
    for (idx = 0; idx < BIRD_FRAME_W_BYTES; idx++)
    {
        uint8_t ink;
        if (section_background_tree_bracket_ink((uint8_t)(col + idx), y, row_max, &ink))
        {
            uint8_t row;
            for (row = 0; row < BIRD_FRAME_H; row++)
                bird_frame_scratch[(uint16_t)row * BIRD_FRAME_W_BYTES + idx] = 0xFF;
        }
    }
    return bird_frame_scratch;
}

void section_bird_init(const HiresBitmap *screen)
{
    HxsprColor color;
    const uint8_t *frame;
    bird_y = bird_y_for_angle(bird_angle);
    color = bird_color_for(bird_col, bird_y);
    frame = bird_prepare_frame(bird_walk, bird_col, bird_y);
    hxspr_draw(screen, frame, BIRD_FRAME_W_BYTES, BIRD_FRAME_H, bird_col, bird_y,
               HXSPR_OR_SPARSE, bird_body_backup, &color, bird_color_backup);
}

void section_bird_tick(const HiresBitmap *screen)
{
    HxsprColor color;
    const uint8_t *frame;

    bird_wait(BIRD_STEP_DELAY);

    frame = bird_prepare_frame(bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE, bird_col, bird_y);
    hxspr_erase(screen, frame, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
                bird_col, bird_y, HXSPR_OR_SPARSE, bird_body_backup, &bird_color, bird_color_backup);

    bird_frame++;
    if (bird_frame >= BIRD_FRAME_COUNT)
        bird_frame = 0;
    bird_col++;
    if (bird_col > BIRD_MAX_COL)
        bird_col = BIRD_MIN_COL;
    bird_angle = (uint8_t)(bird_angle + BIRD_ANGLE_STEP);
    bird_y = bird_y_for_angle(bird_angle);

    color = bird_color_for(bird_col, bird_y);
    frame = bird_prepare_frame(bird_walk + (uint16_t)bird_frame * BIRD_FRAME_SIZE, bird_col, bird_y);
    hxspr_draw(screen, frame, BIRD_FRAME_W_BYTES, BIRD_FRAME_H,
               bird_col, bird_y, HXSPR_OR_SPARSE, bird_body_backup, &color, bird_color_backup);
}
