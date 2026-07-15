// section_func3d.c - see section_func3d.h.
//
// Inspired by Oscar64's own samples/hires/func3d.c -- a static, once-built
// 3D height-field render (a rippling surface function, `f = -cos(r*16) *
// exp(-2*r)`, projected through a perspective camera via gfx/vector3d.h,
// hidden-surface-removed with a painter's-algorithm depth sort, and
// rendered as SHADED, outlined quads). Kept: the same height function, the
// same vector3d.h projection pipeline (mat4_make_perspective/
// mat4_set_rotate_x/_y/mat4_rmmul/mat4_mmul/vec3_project -- all confirmed
// working under this project's own -rt=include/oric_crt_hires.c runtime
// via a real standalone build+Phosphoric run, not just a compile check --
// see ~/.claude/plans/sorted-swinging-thacker.md's Round 6 section), and
// the same progressive on-screen status captions. NOT kept: the
// painter's-algorithm qsort + per-quad lighting/shading + quad-fill --
// Oric HIRES has no per-pixel/per-cell colour to shade a fill with (see
// hires.h's own header comment on ink/paper being a per-ROW-SPAN serial
// attribute), and a pure WIREFRAME mesh needs no hidden-surface removal
// at all (line draw order doesn't matter visually) -- so this section
// skips straight from projected vertices to drawing the mesh's row-wise
// and column-wise connecting lines, dropping func3d.c's own "Sorting
// surfaces" caption along with the depth-sort it described.
//
// GRID_SIZE=9 (an 8x8-quad, 81-vertex mesh) is deliberately far smaller
// than Oscar64's own C64 sample's SIZE=31 (961 vertices) -- the Oric's
// 1 MHz clock makes that scale impractical for a demo section that should
// build in a few seconds, not tie up the whole section's hold time.
//
// A REAL, confirmed regression was found and fixed while adding this
// section's own place in the section table (Round 6, after
// section_polygon_workout.c already existed and had been verified
// working): once `section_sprite_showcase` was ALSO added elsewhere in
// the program, this section's own mesh started rendering intermittently
// -- sometimes a full, correct mesh, sometimes nothing at all -- despite
// draw_mesh_line()'s own loop counters (line_index, and a temporary
// settled-sample probe logging into a ring buffer right after tick()
// returns, not an arbitrary async cycle count -- see
// section_polygon_workout.c's own header comment for why that distinction
// matters) always reaching their correct final values. Deterministic
// VRAM reads at multiple fixed cycle counts confirmed this wasn't a
// sampling artifact: the mesh was GENUINELY absent from the framebuffer
// at some points and genuinely present at others, for the exact same
// code, only differing in what ELSE existed elsewhere in the whole
// program -- another instance of this project's own well-documented
// Oscar64 -O2 whole-program register-allocator bug class (see
// ~/.claude/oscar64.md), this time apparently timing/interrupt-sensitive
// (Arkos's `arkos_tick()`/`main_frame_tick_isr()` fire continuously via
// `hrirq.h` throughout). Fixed by BOTH: (1) replacing hires.c's own
// hb_line() with a local, `__noinline` hand-written Bresenham
// (`draw_line_local()` below, calling hb_set()/hb_clr() directly -- the
// same proven-safe primitives hb_rect_fill()/hb_ellipse_fill() already
// use successfully elsewhere), and (2) bracketing every mesh
// erase/recompute/draw pass with hrirq_stop()/hrirq_start(), matching the
// interrupt-collision mitigation this project has used before. Verified
// via the same settled-sample ring-buffer technique: a stable, correct
// count on every sampled tick after both fixes, across a long soak test
// on both targets -- see this project's own plan file (Round 6) for the
// full investigation log.
//
// Round 10 follow-up, per real user-reported audible regression: the
// original fix above bracketed EVERY line of a whole 144-line
// erase/redraw pass (F3D_ROTATE) or an 8-line-per-tick batch (F3D_DRAW)
// with a SINGLE hrirq_stop()/hrirq_start() pair around the entire batch.
// hrirq_stop()/hrirq_start() are plain SEI/CLI (rasterirq.c) -- and Timer
// 1's hardware interrupt-pending flag is a single latched bit, not a
// counter: however many real 100Hz timer periods elapse while SEI'd, at
// most ONE arkos_tick() call happens once CLI'd again, so a long SEI
// window doesn't just delay Arkos, it permanently DROPS however many
// ticks would have fired during it. Narrowing the bracket to wrap each
// INDIVIDUAL draw/erase/project call (not the whole batch) reduced but
// did NOT eliminate the audible slowdown, per further user feedback --
// F3D_ROTATE still did a full 288-operation erase+recompute+redraw pass
// in a single main-loop tick, just with 288 tiny SEI/CLI pairs instead of
// one big one, still adding up to real, measurable dropped-tick time.
//
// Round 11 follow-up: tried removing the interrupt brackets from
// F3D_ROTATE ENTIRELY as an experiment (soak-tested + screenshot-verified
// no corruption reappeared -- the Round 6 regression's own signature,
// mesh randomly present/absent for the SAME code, never showed up; every
// sample was either a clean complete mesh or a normal, explicable
// mid-erase/mid-redraw transient). But it revealed a DIFFERENT real cost:
// with no protection at all, Arkos's IRQ can now fire freely throughout
// the 288-operation batch, and each individual firing adds real interrupt
// overhead (save/dispatch/restore) ON TOP of the batch's own work --
// confirmed via closely-spaced screenshots that the mesh sat visibly
// blank (mid erase-to-redraw) for a MUCH longer real-time stretch than
// before, meaning the whole batch now took far longer wall-clock time to
// complete, not less. Neither extreme (one huge SEI window vs. no
// protection at all around one huge batch) is a good trade -- the first
// starves Arkos, the second makes the visual redraw itself much slower.
//
// The actual fix: reduce how much work is bunched into any ONE tick, the
// same way F3D_DRAW's own build phase already paces itself
// (LINES_PER_TICK lines per tick, not all 144 at once). F3D_ROTATE is
// now its own small state machine (F3D_ROTATE_ERASE -> _RECOMPUTE ->
// _DRAW -> back to _ERASE for the next step), each state processing only
// ROTATE_LINES_PER_TICK lines (or, for _RECOMPUTE, GRID_SIZE cheap
// project_row() calls) per tick instead of the full 144/9. This directly
// answers "why do we need so much IRQ safe room" -- we don't, once no
// single tick's own workload is large enough for the SEI time (per-line,
// same narrow brackets as before) to add up to anything perceptible.
// Confirmed via soak test + screenshots: the mesh now updates
// continuously in small, steady steps every tick (reading as a
// sweeping/wipe-style rotation rather than a periodic "jump" every few
// ticks) with no extended blank stretches and no corruption.
//
// Round 13 follow-up, per explicit user request ("is there really no way
// to draw 3d function without disabling temporarily music?"): with
// ROTATE_LINES_PER_TICK already this small, removed the per-line
// hrirq_stop()/hrirq_start() brackets ENTIRELY (an experiment Round 11
// also tried, but only against the OLD large-288-line-batch design, where
// it made the redraw itself much slower from unprotected-IRQ overhead
// piling up across one huge batch -- never re-tested against today's
// already-small-batched code). Re-verified via a fresh soak test against
// TODAY's code specifically: the Round 6 mesh-corruption failure mode
// (mesh randomly absent/garbled for the same code) did NOT reappear --
// every sampled tick showed a clean, coherent mesh (complete, mid-erase,
// or mid-redraw, never garbled) across the section's own rotate phase.
// Audible/AY-register tempo impact couldn't be directly instrumented in
// this session (no debug-symbol AY-shadow address available), but the
// mechanism that caused the ORIGINAL audible slowdown (one huge
// contiguous 288-line SEI window) is structurally gone regardless --
// each tick's own interrupt-free stretch is now inherently tiny (12 lines
// worth of erase/draw, or 9 project_row() calls), the same small unit
// size already proven not to cause a perceptible slowdown WITH brackets,
// so removing brackets around already-small units only ever adds a
// little interrupt-dispatch overhead, not a new large blocking window.
// If a future playtest ever reports func3d audio trouble again, the fix
// is a straight revert of just this one change (put hrirq_stop()/
// hrirq_start() back around the three ROTATE loop bodies) -- do not
// reach for a different mitigation without re-confirming this is really
// the section at fault first.

#include <math.h>
#include "oric.h"
#include "hires.h"
#include "vector3d.h"
#include "section_func3d.h"

#define GRID_SIZE   9u
#define GRID_LAST   (GRID_SIZE - 1u)   // 8 -- power of 2, cheap / and % below
#define HALF_F      4.0f

// A "bit larger" footprint (was 70.0f) and taller "mountains" (the height
// term's own scale below, was 0.5f), both per explicit user feedback.
// PROJ_SCALE only affects on-screen size, not world height, so it's a
// separate knob from HEIGHT_SCALE.
#define PROJ_SCALE_X 82.0f
#define PROJ_SCALE_Y 82.0f
#define PROJ_CX     120u
#define PROJ_CY     108u
#define HEIGHT_SCALE 0.85f

#define TOTAL_LINES ((uint16_t)GRID_LAST * GRID_SIZE * 2u)   // 144

#define LINES_PER_TICK      8u    // build-phase draw rate
// Radians added to rot_y per FULL erase+recompute+redraw cycle (not per
// tick -- see F3D_ROTATE_RECOMPUTE below). Bumped from an earlier 0.06f:
// spreading each cycle's own drawing across many more, smaller ticks
// (ROTATE_LINES_PER_TICK below) means a full cycle now spans more real
// ticks than the old single-tick "jump" design, so the per-cycle step is
// increased to keep the overall rotation SPEED (radians per real second)
// in the same ballpark, not slower just because of the redesign.
#define ROTATE_STEP     0.20f
// Erase/draw batch size per tick during F3D_ROTATE_ERASE/_DRAW -- same
// pacing convention as LINES_PER_TICK above, chosen to keep each tick's
// own workload small. See this file's own header comment for the full
// redesign rationale (this batch size is also what makes it safe to run
// entirely without hrirq_stop()/hrirq_start() brackets, as of Round 13).
#define ROTATE_LINES_PER_TICK 12u

#define CAPTION_Y  10u
#define CAPTION_H   8u
#define CAPTION_X  12u   // skip column-bytes 0-1 (12px) -- see hires_row_colors()'s own baseline ink/paper attribute bytes

typedef struct { uint8_t x, y; } GPoint;

static Vector3 verts[GRID_SIZE][GRID_SIZE];
static GPoint  proj[GRID_SIZE][GRID_SIZE];
static Matrix4 pmat;        // perspective, computed once at init
static Matrix4 cur_xform;   // pmat * current rotation, recomputed each time rot_y changes
static float   rot_y;

typedef enum { F3D_PREPARE, F3D_PROJECT, F3D_DRAW,
               F3D_ROTATE_ERASE, F3D_ROTATE_RECOMPUTE, F3D_ROTATE_DRAW } F3DState;
static F3DState state;
static uint8_t  row_index;
static uint16_t line_index;

__noinline static void compute_row(uint8_t iy)
{
    uint8_t ix;
    for (ix = 0; ix < GRID_SIZE; ix++)
    {
        float x = ((float)ix - HALF_F) / HALF_F;
        float y = (HALF_F - (float)iy) / HALF_F;
        float r = sqrt(x * x + y * y);
        float f = -cos(r * 16.0f) * exp(-2.0f * r);
        vec3_set(&verts[iy][ix], x, f * HEIGHT_SCALE, y);
    }
}

__noinline static void build_transform(Matrix4 *out, float ry)
{
    Matrix4 rmat, tmat, wmat;
    mat4_ident(&wmat);
    mat4_scale(&wmat, 9.0f);
    mat4_set_rotate_x(&rmat, -0.9f);
    mat4_set_rotate_y(&tmat, ry);
    mat4_rmmul(&rmat, &tmat);
    mat4_rmmul(&rmat, &wmat);
    rmat.m[14] += 20.0f;
    *out = pmat;
    mat4_mmul(out, &rmat);
}

__noinline static void project_row(uint8_t iy)
{
    uint8_t ix;
    for (ix = 0; ix < GRID_SIZE; ix++)
    {
        Vector3 vp;
        vec3_project(&vp, &cur_xform, &verts[iy][ix]);
        proj[iy][ix].x = (uint8_t)(vp.v[0] * PROJ_SCALE_X + PROJ_CX);
        proj[iy][ix].y = (uint8_t)(vp.v[1] * PROJ_SCALE_Y + PROJ_CY);
    }
}

__noinline static void draw_line_local(const HiresBitmap *hb, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set)
{
    int16_t dx = (int16_t)x1 - (int16_t)x0;
    int16_t dy = (int16_t)y1 - (int16_t)y0;
    int16_t sx = (dx < 0) ? -1 : 1;
    int16_t sy = (dy < 0) ? -1 : 1;
    int16_t err;
    int16_t x = x0, y = y0;

    if (dx < 0) dx = (int16_t)(-dx);
    if (dy < 0) dy = (int16_t)(-dy);
    err = (int16_t)(dx - dy);

    for (;;)
    {
        if (set)
            hb_set(hb, (uint8_t)x, (uint8_t)y);
        else
            hb_clr(hb, (uint8_t)x, (uint8_t)y);

        if (x == x1 && y == y1)
            break;

        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 > -dy) { err = (int16_t)(err - dy); x = (int16_t)(x + sx); }
            if (e2 <  dx) { err = (int16_t)(err + dx); y = (int16_t)(y + sy); }
        }
    }
}

// Mesh line #n out of TOTAL_LINES: lines 0..(GRID_SIZE*GRID_LAST-1) are
// horizontal (row-wise) segments, the rest are vertical (column-wise).
// GRID_LAST=8 is a power of 2, so the / and % below are cheap shifts/masks,
// not real divmod calls.
__noinline static void draw_mesh_line(const HiresBitmap *screen, uint16_t n, bool set)
{
    uint16_t horiz_count = (uint16_t)GRID_SIZE * GRID_LAST;
    if (n < horiz_count)
    {
        uint8_t iy = (uint8_t)(n / GRID_LAST);
        uint8_t ix = (uint8_t)(n % GRID_LAST);
        draw_line_local(screen, proj[iy][ix].x, proj[iy][ix].y, proj[iy][ix + 1].x, proj[iy][ix + 1].y, set);
    }
    else
    {
        uint16_t m = (uint16_t)(n - horiz_count);
        uint8_t ix = (uint8_t)(m / GRID_LAST);
        uint8_t iy = (uint8_t)(m % GRID_LAST);
        draw_line_local(screen, proj[iy][ix].x, proj[iy][ix].y, proj[iy + 1][ix].x, proj[iy + 1][ix].y, set);
    }
}

static void clear_caption(const HiresBitmap *screen)
{
    hb_rect_fill(screen, (const HiresClip *)0, CAPTION_X, CAPTION_Y, (uint8_t)(HIRES_WIDTH_PX - CAPTION_X), CAPTION_H, false);
}

static void set_caption(const HiresBitmap *screen, const char *text, uint8_t len)
{
    clear_caption(screen);
    hb_put_chars_center(screen, (const HiresClip *)0, CAPTION_Y, text, len);
}

void section_func3d_init(const HiresBitmap *screen)
{
    uint8_t y;

    hb_fill(screen, 0x40);   // real RAM isn't zero-initialized -- start blank
    for (y = 0; y < HIRES_ROWS; y++)
        hires_row_colors(y, A_FWWHITE, A_BGBLACK);

    mat4_make_perspective(&pmat, 0.5f * (float)PI, 1.0f, 0.0f, 200.0f);
    rot_y       = 0.0f;
    state       = F3D_PREPARE;
    row_index   = 0;
    line_index  = 0;

    set_caption(screen, "Preparing function", 18);
}

void section_func3d_tick(const HiresBitmap *screen)
{
    switch (state)
    {
    case F3D_PREPARE:
        compute_row(row_index);
        row_index++;
        if (row_index >= GRID_SIZE)
        {
            row_index = 0;
            state = F3D_PROJECT;
            build_transform(&cur_xform, rot_y);
            set_caption(screen, "Projecting vertices", 20);
        }
        break;

    case F3D_PROJECT:
        project_row(row_index);
        row_index++;
        if (row_index >= GRID_SIZE)
        {
            line_index = 0;
            state = F3D_DRAW;
            set_caption(screen, "Drawing surfaces", 17);
        }
        break;

    case F3D_DRAW:
    {
        uint8_t n;
        for (n = 0; n < LINES_PER_TICK && line_index < TOTAL_LINES; n++, line_index++)
        {
            draw_mesh_line(screen, line_index, true);
        }
        if (line_index >= TOTAL_LINES)
        {
            state = F3D_ROTATE_ERASE;
            line_index = 0;
            clear_caption(screen);
        }
        break;
    }

    // The three F3D_ROTATE_* states below form one continuous cycle
    // (ERASE -> RECOMPUTE -> DRAW -> back to ERASE), each processing only
    // a small, bounded amount of work per tick -- see this file's own
    // header comment for why (spreading the load is what removes the
    // need for a large interrupt-disabled window, not the bracket
    // placement itself).
    case F3D_ROTATE_ERASE:
    {
        uint16_t n;
        for (n = 0; n < ROTATE_LINES_PER_TICK && line_index < TOTAL_LINES; n++, line_index++)
        {
            draw_mesh_line(screen, line_index, false);   // erase the OLD mesh
        }
        if (line_index >= TOTAL_LINES)
            state = F3D_ROTATE_RECOMPUTE;
        break;
    }

    case F3D_ROTATE_RECOMPUTE:
    {
        uint8_t iy;
        rot_y = rot_y + ROTATE_STEP;
        build_transform(&cur_xform, rot_y);
        for (iy = 0; iy < GRID_SIZE; iy++)
        {
            project_row(iy);
        }
        line_index = 0;
        state = F3D_ROTATE_DRAW;
        break;
    }

    case F3D_ROTATE_DRAW:
    {
        uint16_t n;
        for (n = 0; n < ROTATE_LINES_PER_TICK && line_index < TOTAL_LINES; n++, line_index++)
        {
            draw_mesh_line(screen, line_index, true);    // draw the NEW mesh
        }
        if (line_index >= TOTAL_LINES)
        {
            line_index = 0;
            state = F3D_ROTATE_ERASE;
        }
        break;
    }
    }
}
