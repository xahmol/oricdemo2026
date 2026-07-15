// section_func3d.c - see section_func3d.h.
//
// Inspired by Oscar64's own samples/hires/func3d.c -- a static, once-built
// 3D height-field render (a rippling surface function, `f = -cos(r*16) *
// exp(-2*r)`, projected through a perspective camera via gfx/vector3d.h)
// and the same progressive on-screen status captions. NOT kept: the
// original's painter's-algorithm qsort + per-quad lighting/shading +
// quad-fill -- Oric HIRES has no per-pixel/per-cell colour to shade a
// fill with (see hires.h's own header comment on ink/paper being a
// per-ROW-SPAN serial attribute), and a pure WIREFRAME mesh needs no
// hidden-surface removal at all (line draw order doesn't matter
// visually) -- so this section skips straight from projected vertices to
// drawing the mesh's row-wise and column-wise connecting lines.
//
// GRID_SIZE=9 (an 8x8-quad, 81-vertex mesh) is deliberately far smaller
// than Oscar64's own C64 sample's SIZE=31 (961 vertices) -- the Oric's
// 1 MHz clock makes that scale impractical for a demo section that should
// build in a few seconds, not tie up the whole section's hold time.
//
// Lines are drawn via a LOCAL, `__noinline` hand-written Bresenham
// (`draw_line_local()` below, calling hb_set()/hb_clr() directly), NOT
// hires.c's own hb_line() -- a real, confirmed Oscar64 -O2 whole-program
// register-allocator bug made hb_line()'s own call site here render the
// mesh correctly on SOME ticks and leave it genuinely absent on others,
// for the exact same code, depending only on what ELSE existed elsewhere
// in the whole program (see ~/.claude/oscar64.md for the full
// investigation). Do not swap this back to hb_line() without re-running
// a long soak test across both build targets first.
//
// F3D_ROTATE is its own small state machine (F3D_ROTATE_ERASE ->
// _RECOMPUTE -> _DRAW -> _HOLD -> back to _ERASE), each state processing
// only a small, bounded amount of work per tick (ROTATE_LINES_PER_TICK
// lines, or GRID_SIZE cheap project_row() calls) rather than the mesh's
// full 144 lines/9 rows in one go. This keeps every tick's own workload
// small enough that it runs safely with NO hrirq_stop()/hrirq_start()
// interrupt brackets at all around the erase/recompute/draw calls --
// confirmed via soak test (clean, coherent mesh on every sample, no
// corruption) -- while still leaving Arkos's own 50Hz music tick
// undisturbed (large single-batch designs were tried and rejected during
// development: bracketing one huge batch drops Arkos ticks and audibly
// slows the music, while running one huge batch fully unprotected instead
// makes the redraw itself far slower from accumulated interrupt overhead;
// small per-tick batches avoid both failure modes at once). If a future
// change ever needs to increase ROTATE_LINES_PER_TICK substantially, both
// risks above need re-checking, not just performance.

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
// rationale (this batch size is also what makes it safe to run entirely
// without hrirq_stop()/hrirq_start() brackets).
#define ROTATE_LINES_PER_TICK 12u

// Ticks to hold the fully-drawn mesh before erasing it again -- without
// this, F3D_ROTATE_DRAW's completed result was erased on the very next
// tick, barely visible before disappearing. Same general "admire the
// finished shape for a moment" convention section_hires_showcase.c's own
// PHASE_WAIT_TICKS uses.
#define ROTATE_HOLD_TICKS 15u

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
               F3D_ROTATE_ERASE, F3D_ROTATE_RECOMPUTE, F3D_ROTATE_DRAW,
               F3D_ROTATE_HOLD } F3DState;
static F3DState state;
static uint8_t  row_index;
static uint16_t line_index;
static uint8_t  hold_count;

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
            hold_count = 0;
            state = F3D_ROTATE_HOLD;
        }
        break;
    }

    case F3D_ROTATE_HOLD:
        hold_count++;
        if (hold_count >= ROTATE_HOLD_TICKS)
        {
            line_index = 0;
            state = F3D_ROTATE_ERASE;
        }
        break;
    }
}
