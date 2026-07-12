// section_polygon_workout.c - see section_polygon_workout.h.
//
// Inspired by Oscar64's own samples/hires/polygon.c: a 10-point star
// polygon, alternating outer/inner vertices (radius ratio ~0.4, matching
// the original's own `r = (i & 1) ? 1.0 : 0.4`), continuously rotated and
// pulsed in size. The original fills each of its 128 frames with a
// shifting grey shade (`NineShadesOfGrey`) -- not reproduced here: Oric
// HIRES has no per-pixel/per-cell colour to shade a fill with (ink/paper
// is a serial per-ROW-SPAN attribute, see hires.h's own header comment),
// and Round 5 found hb_polygon_fill() has a real, unresolved Oscar64 -O2
// whole-program register-allocator bug that only manifests at SOME real
// call sites (silently drops most of its fill loop's iterations) -- see
// ~/.claude/plans/sorted-swinging-thacker.md's "Round 6" section and
// ~/.claude/oscar64.md for the full writeup. This section is WIREFRAME
// ONLY (hb_line(), not hb_polygon_fill()) specifically to sidestep that
// risk entirely, not just for stylistic reasons.
//
// hb_line() itself was verified safe at this file's own real call depth
// (Round 6 plan's own mandatory gate 1) via a minimal single-call smoke
// test, RAM-dump-verified pixel-exact (no dropped iterations), THEN
// re-verified again at the full effect's own repeated-per-tick,
// computed-coordinate call shape below (a meaningfully different shape
// from the smoke test's two fixed, single calls from init() -- per this
// project's own hard-won lesson, never trust one call shape's safety to
// predict another's). A NON-obvious methodology pitfall surfaced during
// that second verification pass, worth recording: an async
// `--dump-ram-at` sample of a LIVE, continuously-incrementing per-tick
// counter (a debug edge-count probe inside draw_star_edges()) showed
// varying, seemingly-wrong values (2-10 out of an expected 10) at
// different arbitrary cycle points -- which looked exactly like the
// hb_polygon_fill bug's own signature. Switching to hires.c's own
// hb_line() to a local hand-written Bresenham (hb_set()/hb_clr()
// directly) made no difference, which was the tell: the counter was
// being sampled MID-EXECUTION of an in-progress draw loop (each edge
// takes real, measurable time to draw), not after it had settled. Logging
// the counter's value into a small ring buffer at the ONE genuinely
// settled moment -- immediately after `section->tick()` returns in
// `main.c`'s `run_section()`, not at an arbitrary async cycle count --
// showed a clean 10/10 on every one of the first 64 ticks. There was
// never a real bug here: `hb_line()` (and this file's own
// `draw_line_local()`, since removed) both work correctly at this call
// site. Kept as a permanent cautionary note: a LIVE counter needs a
// settled-moment capture technique, not an arbitrary-cycle RAM dump --
// the technique that correctly caught the REAL Round 5 hb_polygon_fill
// bug (a ONE-TIME init-time call, whose result persists unchanged for the
// rest of that section's run, so an arbitrary later sample IS reliable
// there) does not generalize to a counter incrementing every tick.

#include "oric.h"
#include "hires.h"
#include "fixedmath.h"
#include "section_polygon_workout.h"

#define STAR_POINTS       10u
#define STAR_CENTER_X    120u
#define STAR_CENTER_Y    100u
#define STAR_OUTER_MIN    40u   // smallest outer radius (fully "pulsed in")
#define STAR_OUTER_RANGE  30u   // outer radius pulses STAR_OUTER_MIN..+RANGE
#define ANGLE_STEP         2u   // rotation speed, per tick
#define PULSE_STEP         1u   // size-pulse speed, per tick (slower than rotation feel -- see oric_cos's own 256-unit period)

#define COLOR_PALETTE_SIZE 4u   // power of 2 -- cheap bitwise-AND cycling, same technique as section_logo.c's bar palettes
#define COLOR_CYCLE_EVERY  8u   // main-loop ticks between each colour-palette step

// Bounding row range the star can ever occupy: CENTER_Y +/- (OUTER_MIN+RANGE).
#define STAR_BAND_TOP    (STAR_CENTER_Y - (STAR_OUTER_MIN + STAR_OUTER_RANGE))
#define STAR_BAND_BOTTOM (STAR_CENTER_Y + (STAR_OUTER_MIN + STAR_OUTER_RANGE))

// Each point's own angle offset from the current rotation angle -- 256/10=25.6,
// rounded to the nearest integer per point (0,26,51,77,102,128,154,179,205,230)
// rather than an exact 25.6 fractional step, since oric_sin/oric_cos only take
// uint8_t "angle units". Close enough to a regular 10-point star that the
// rounding is not visually distinguishable at HIRES resolution.
static const uint8_t point_angle_offset[STAR_POINTS] =
    { 0, 26, 51, 77, 102, 128, 154, 179, 205, 230 };

static const uint8_t color_palette[COLOR_PALETTE_SIZE] =
    { A_FWCYAN, A_FWMAGENTA, A_FWYELLOW, A_FWWHITE };

typedef struct { uint8_t x, y; } StarPoint;

static StarPoint pts[STAR_POINTS];
static uint8_t   rot_angle;
static uint8_t   pulse_angle;
static uint8_t   color_phase;
static uint8_t   color_wait;

// __noinline: computes STAR_POINTS fresh vertex positions per call, each
// via oric_cos/oric_sin table lookups + a 16-bit multiply/divide -- called
// from tick() every frame, not a compile-time-constant call site, so this
// is defense-in-depth (see this file's own header comment) rather than a
// proven-necessary fix.
__noinline static void compute_star(uint8_t rot, uint8_t pulse, StarPoint *out)
{
    uint8_t outer = (uint8_t)(STAR_OUTER_MIN + (((uint16_t)((uint8_t)(oric_cos(pulse) + 128)) * STAR_OUTER_RANGE) >> 8));
    uint8_t inner = (uint8_t)((outer * 2) / 5);   // ~0.4 ratio, matching polygon.c's own r=0.4
    uint8_t i;

    for (i = 0; i < STAR_POINTS; i++)
    {
        uint8_t ang = (uint8_t)(rot + point_angle_offset[i]);
        uint8_t r = (i & 1) ? inner : outer;
        int16_t cx = (int16_t)((int16_t)oric_cos(ang) * (int16_t)r) / 127;
        int16_t cy = (int16_t)((int16_t)oric_sin(ang) * (int16_t)r) / 127;
        out[i].x = (uint8_t)(STAR_CENTER_X + cx);
        out[i].y = (uint8_t)(STAR_CENTER_Y + cy);
    }
}

__noinline static void draw_star_edges(const HiresBitmap *screen, const StarPoint *p, bool set)
{
    uint8_t i;
    for (i = 0; i < STAR_POINTS; i++)
    {
        uint8_t j = (uint8_t)(i + 1);
        if (j == STAR_POINTS) j = 0;
        hb_line(screen, (const HiresClip *)0, p[i].x, p[i].y, p[j].x, p[j].y, set);
    }
}

void section_polygon_workout_init(const HiresBitmap *screen)
{
    uint8_t y;

    hb_fill(screen, 0x40);   // real RAM isn't zero-initialized -- start blank
    for (y = 0; y < HIRES_ROWS; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);

    rot_angle   = 0;
    pulse_angle = 0;
    color_phase = 0;
    color_wait  = 0;

    compute_star(rot_angle, pulse_angle, pts);
    draw_star_edges(screen, pts, true);

    for (y = STAR_BAND_TOP; y <= STAR_BAND_BOTTOM; y++)
        hires_row_colors(y, color_palette[color_phase], A_BGBLACK);
}

// void, not bool -- see section_common.h's own header comment for why.
// Never calls section_mark_finished(): circles indefinitely, like
// section_logo.c's own raster bars -- paced externally by main.c's own
// min_ticks/max_ticks.
void section_polygon_workout_tick(const HiresBitmap *screen)
{
    draw_star_edges(screen, pts, false);   // erase the OLD frame first

    rot_angle   = (uint8_t)(rot_angle + ANGLE_STEP);
    pulse_angle = (uint8_t)(pulse_angle + PULSE_STEP);
    compute_star(rot_angle, pulse_angle, pts);

    draw_star_edges(screen, pts, true);    // draw the NEW frame

    color_wait++;
    if (color_wait >= COLOR_CYCLE_EVERY)
    {
        uint8_t y;
        color_wait = 0;
        color_phase = (uint8_t)((color_phase + 1) & (COLOR_PALETTE_SIZE - 1u));
        for (y = STAR_BAND_TOP; y <= STAR_BAND_BOTTOM; y++)
            hires_row_colors(y, color_palette[color_phase], A_BGBLACK);
    }
}
