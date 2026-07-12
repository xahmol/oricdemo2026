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
// hb_line() is reused here (already proven safe, both at an isolated
// smoke-test call site AND at section_polygon_workout.c's own
// repeated-per-tick, computed-coordinate call shape -- see that file's
// header comment for the full verification story, including a real
// methodology pitfall found and corrected along the way).

#include <math.h>
#include "oric.h"
#include "hires.h"
#include "vector3d.h"
#include "section_func3d.h"

#define GRID_SIZE   9u
#define GRID_LAST   (GRID_SIZE - 1u)   // 8 -- power of 2, cheap / and % below
#define HALF_F      4.0f

#define PROJ_SCALE_X 70.0f
#define PROJ_SCALE_Y 70.0f
#define PROJ_CX     120u
#define PROJ_CY     110u

#define TOTAL_LINES ((uint16_t)GRID_LAST * GRID_SIZE * 2u)   // 144

#define LINES_PER_TICK      8u    // build-phase draw rate
#define ROTATE_STEP     0.06f     // radians per rotation step
#define ROTATE_EVERY_TICKS  4u    // ticks between each rotation redraw

#define CAPTION_Y  10u
#define CAPTION_H   8u
#define CAPTION_X  12u   // skip column-bytes 0-1 (12px) -- see hires_row_colors()'s own baseline ink/paper attribute bytes

typedef struct { uint8_t x, y; } GPoint;

static Vector3 verts[GRID_SIZE][GRID_SIZE];
static GPoint  proj[GRID_SIZE][GRID_SIZE];
static Matrix4 pmat;        // perspective, computed once at init
static Matrix4 cur_xform;   // pmat * current rotation, recomputed each time rot_y changes
static float   rot_y;
static uint8_t rotate_wait;

typedef enum { F3D_PREPARE, F3D_PROJECT, F3D_DRAW, F3D_ROTATE } F3DState;
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
        vec3_set(&verts[iy][ix], x, f * 0.5f, y);
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
        hb_line(screen, (const HiresClip *)0, proj[iy][ix].x, proj[iy][ix].y, proj[iy][ix + 1].x, proj[iy][ix + 1].y, set);
    }
    else
    {
        uint16_t m = (uint16_t)(n - horiz_count);
        uint8_t ix = (uint8_t)(m / GRID_LAST);
        uint8_t iy = (uint8_t)(m % GRID_LAST);
        hb_line(screen, (const HiresClip *)0, proj[iy][ix].x, proj[iy][ix].y, proj[iy + 1][ix].x, proj[iy + 1][ix].y, set);
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
    rotate_wait = 0;
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
            draw_mesh_line(screen, line_index, true);
        if (line_index >= TOTAL_LINES)
        {
            state = F3D_ROTATE;
            rotate_wait = 0;
            clear_caption(screen);
        }
        break;
    }

    case F3D_ROTATE:
        rotate_wait++;
        if (rotate_wait >= ROTATE_EVERY_TICKS)
        {
            uint16_t n;
            uint8_t iy;

            rotate_wait = 0;

            for (n = 0; n < TOTAL_LINES; n++)
                draw_mesh_line(screen, n, false);   // erase the OLD mesh

            rot_y = rot_y + ROTATE_STEP;
            build_transform(&cur_xform, rot_y);
            for (iy = 0; iy < GRID_SIZE; iy++)
                project_row(iy);

            for (n = 0; n < TOTAL_LINES; n++)
                draw_mesh_line(screen, n, true);    // draw the NEW mesh
        }
        break;
    }
}
