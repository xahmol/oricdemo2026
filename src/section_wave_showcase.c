// section_wave_showcase.c - see section_wave_showcase.h.
//
// assets/oricmag.bin: a real vintage magazine scan of a full Oric Atmos
// setup (monitor, keyboard, Microdisc drive, printer), converted with
// tools/oric_pictconv.py's new `pictoric` mode (--no-inverse-attr) --
// this project's port of Samuel Devulder's PictOric algorithm, verified
// against the real upstream Lua tool's own output (see docs/pictconv.md's
// "Verifying against upstream references" section for the full story,
// including two real bugs found and fixed by running that comparison).
// Loaded at runtime via include/picture.h, same convention as every other
// section's own picture.
//
// The wave effect: each tick, every row is horizontally ROTATED (not
// shifted-with-fill) by a small, per-row-BAND, sine-varying amount,
// giving the photo a "heat haze" / "flag waving" distortion -- a classic
// 8-bit demo technique, and a genuinely good fit for this specific
// picture: motion makes photographic dithering noise (this picture's own
// scan-texture-on-top-of-photo-noise is the busiest source image this
// project has converted) far less noticeable than it is as a still frame.
//
// Two deliberate design choices, both driven by this project's own
// established constraints, not preference:
//
//   1. ROTATION, not a plain shift-with-fill (hb_scroll_left/right's own
//      convention). A picture this dense in real per-row attribute bytes
//      (not just a couple of ink-bracket columns like the hand-drawn
//      assets) has no "spare" pixel content to sacrifice every tick, and
//      a full off-screen backup copy to redraw from (this project's other
//      option for a lossless shift) would cost another 8000 bytes of
//      RAM -- already tight (~5.5KB headroom before this phase). A
//      circular rotation is fully reversible (composes: rotating by A
//      then B is the same as rotating by A+B mod row width) and needs
//      only a tiny (few-byte) temporary buffer per row, since only the
//      wrapped-around bytes need saving -- no backup copy, no picture
//      degradation no matter how many ticks run.
//   2. BANDED (BAND_HEIGHT rows share one shift amount), not per-scanline.
//      A true per-scanline wave would mean up to 200 independent row
//      rotations every tick; banding cuts that to 200/BAND_HEIGHT while
//      still reading as a clear wave, the same "coarse but readable"
//      trade-off real 8-bit wave effects made when hardware fine-scroll
//      wasn't available (the Oric's HIRES bitmap has none).
//
// Rotating a row's full byte content (whatever mix of attribute and pixel
// bytes it currently holds) is safe: an Oric HIRES attribute byte doesn't
// care what COLUMN it sits at, only that it precedes the content it's
// meant to recolour -- rotating the whole row just repositions where each
// attribute's own "start recolouring from here" point falls, which reads
// as part of the wave distortion itself, not corruption.

#include <string.h>
#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "fixedmath.h"
#include "section_wave_showcase.h"

#ifdef STORAGE_FLOPPY
#define ORICMAG_FILE 6
#else
#define ORICMAG_FILE "oricmag.bin"
#endif

#define BAND_HEIGHT      5u                              // rows sharing one shift amount
#define NUM_BANDS        (uint8_t)(HIRES_ROWS / BAND_HEIGHT)
#define ANGLE_PER_BAND   9u                               // angle units between adjacent bands
#define PHASE_STEP       3u                               // angle units advanced per tick
// Deliberately NOT "2u": a real bug (found via a soak-test hang) came
// from mixing this constant, unsigned, into an expression with a SIGNED
// value (oric_cos()'s own int8_t result) -- C's usual arithmetic
// conversions silently promote the WHOLE expression to unsigned when a
// signed and unsigned operand of the same rank are mixed, which breaks
// negative-value handling entirely (a negative cos() value's bit pattern
// gets reinterpreted as a huge positive number before the division that
// was meant to bring it back into -MAX_AMPLITUDE..+MAX_AMPLITUDE range).
// Confirmed via a live RAM dump mid-hang: band_shift[] held impossible
// values outside that intended range. Plain signed `2` throughout keeps
// the whole `target` computation in signed arithmetic.
#define MAX_AMPLITUDE    2                                // max rotation, in byte-columns (12px)

// A single tick's rotation is a DELTA between last tick's target and this
// tick's target, each independently in [-MAX_AMPLITUDE, +MAX_AMPLITUDE] --
// so the delta itself can be up to 2*MAX_AMPLITUDE (e.g. swinging from
// -2 straight to +2 in one tick).
#define MAX_ROTATE_DELTA (uint8_t)(2u * MAX_AMPLITUDE)

static uint8_t wave_phase;
static int8_t band_shift[NUM_BANDS];   // last-applied rotation per band, for delta tracking

// A real bug found here via a genuine soak-test hang (PC parked mid-
// function, identical across widely different cycle budgets -- this
// project's own established hang signature): this was originally a
// FUNCTION-LOCAL array (`uint8_t tmp[MAX_ROTATE_DELTA];` inside each
// rotate helper below). Oscar64's whole-program compiler shares stack
// storage between different functions' own locals when its call-graph
// analysis concludes they're never simultaneously live (confirmed via
// the build's own .map file: address $97F0 was shared between this
// code's own local array and unrelated functions like scroller_tick's
// own "clip" and exp()/sqrt()'s own "x" parameter) -- and something about
// this new function wasn't sized/accounted for correctly in that sharing
// scheme, corrupting whichever unrelated variable happened to share the
// slot. A MODULE-LEVEL STATIC array instead gets one fixed, permanent,
// never-shared BSS address for the whole program's lifetime, sidestepping
// this entire class of allocator risk -- the same reasoning this project
// has applied before to similar whole-program allocator surprises (see
// ~/.claude/oscar64.md). Costs MAX_ROTATE_DELTA (4) bytes of permanent
// RAM instead of borrowed stack space -- negligible.
static uint8_t rotate_tmp[MAX_ROTATE_DELTA];

// Rotates one HIRES row's 40 bytes right by n (1..MAX_ROTATE_DELTA) byte-
// columns -- the last n bytes wrap around to the front. __noinline as a
// defensive measure: this project has a documented history of Oscar64
// -O2 silently mis-inlining loops that compute per-row HIRES addresses
// (~/.claude/oscar64.md), so any new hot loop touching row data gets this
// same treatment rather than being assumed safe by analogy.
static __noinline void rotate_row_right(uint8_t *row, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++)
        rotate_tmp[i] = row[HIRES_ROW_BYTES - n + i];
    memmove(row + n, row, HIRES_ROW_BYTES - n);
    for (i = 0; i < n; i++)
        row[i] = rotate_tmp[i];
}

static __noinline void rotate_row_left(uint8_t *row, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++)
        rotate_tmp[i] = row[i];
    memmove(row, row + n, HIRES_ROW_BYTES - n);
    for (i = 0; i < n; i++)
        row[HIRES_ROW_BYTES - n + i] = rotate_tmp[i];
}

void section_wave_showcase_init(const HiresBitmap *screen)
{
    uint8_t i;

    picture_load(ORICMAG_FILE, (void *)HIRESVRAM, 8000);

    wave_phase = 0;
    for (i = 0; i < NUM_BANDS; i++)
        band_shift[i] = 0;
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): waves indefinitely, paced
// externally by main.c's own min_ticks/max_ticks (same convention as
// section_logo.c's circling raster bars).
// Extracted into its own __noinline function per this project's own
// established Oscar64 -O2 whole-program bug class: a loop computing a
// per-row address as `base + index*stride` (here, `screen->data +
// (uint16_t)row * HIRES_ROW_BYTES`) can get silently miscompiled when
// left inline inside a larger function -- confirmed here via a real
// soak-test hang: `section_wave_showcase_tick()` got stuck forever
// inside this exact loop on its FIRST call (a live RAM dump at 1.2
// billion cycles showed `wave_phase` still at its post-first-tick value
// and `band_shift[]` only partially populated, mid-band), matching the
// documented precedents in section_logo.c's `set_rows`,
// section_polygon_workout.c's `compute_star`, section_func3d.c's
// `compute_row`/etc. -- see ~/.claude/oscar64.md.
static __noinline void rotate_band(const HiresBitmap *screen, uint8_t row0, int8_t delta)
{
    uint8_t row;
    uint8_t n = (delta > 0) ? (uint8_t)delta : (uint8_t)(-delta);

    for (row = row0; row < row0 + BAND_HEIGHT; row++)
    {
        uint8_t *rowdata = screen->data + (uint16_t)row * HIRES_ROW_BYTES;
        if (delta > 0)
            rotate_row_right(rowdata, n);
        else
            rotate_row_left(rowdata, n);
    }
}

void section_wave_showcase_tick(const HiresBitmap *screen)
{
    uint8_t band;

    wave_phase = (uint8_t)(wave_phase + PHASE_STEP);

    for (band = 0; band < NUM_BANDS; band++)
    {
        uint8_t angle = (uint8_t)(band * ANGLE_PER_BAND + wave_phase);
        // oric_cos(-127..127) -> target in roughly -MAX_AMPLITUDE..+MAX_AMPLITUDE
        int8_t target = (int8_t)((int16_t)oric_cos(angle) * MAX_AMPLITUDE / 127);
        int8_t delta = (int8_t)(target - band_shift[band]);

        if (delta != 0)
        {
            uint8_t row0 = (uint8_t)(band * BAND_HEIGHT);
            rotate_band(screen, row0, delta);
            band_shift[band] = target;
        }
    }
}
