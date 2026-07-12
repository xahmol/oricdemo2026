// section_logo.c - see section_logo.h.
//
// assets/oriclogo.bin: the clean vector "ORIC ATMOS 48K" wordmark from
// Wikimedia Commons (commons.wikimedia.org/wiki/File:Logo_Oric_Atmos.png
// -- Oric International's own vintage company wordmark/hardware
// branding, reproduced here as a non-commercial fan homage, same
// attribution posture as assets/bird.h's own external-asset credit),
// NOT oric.org's own tape-loading screenshot (a noisier, lower-res
// scan tried first -- this source converts far more cleanly). The
// original's red diagonal slash + underline bar were recoloured to
// white before conversion (max-channel brightness threshold, hue-
// independent so the red's own high R channel still counts as "bright") --
// per the user's own explicit direction: this project's serial ink/paper
// attribute bytes can only ever hold ONE ink colour per row-span, so a
// separate red would need its own ink bracket columns the same way
// section_bird.c's letters do, adding real complexity for a static
// logo that doesn't need more than one colour. Converted via
// tools/oric_pictconv.py --mode mono --dither floyd-steinberg (forced
// white ink), full 240x200 HIRES frame (the tool's own --width/--height
// flags currently only accept the full HIRES resolution -- see that
// tool's own --help -- so the logo is resized/composited onto an
// otherwise-blank canvas at its intended screen position, rather than a
// true sub-canvas crop; harmless, since this loads straight into
// HIRESVRAM with nothing else sharing the frame). Loaded at runtime via
// include/picture.h (see that header's own comment for why: keeps this
// ~8000-byte asset out of the ~36KB code/data/BSS budget entirely, same
// reasoning as the Arkos music module). Composited at x=15, 210px wide,
// so real wordmark content spans pixel columns 15-224 -- load-bearing for
// the "no horizontal taper" decision below, not just a stray fact.
//
// Two rotating 3D-look bars: bar_a and bar_b each circle through the
// logo's own row range (LOGO_TOP..LOGO_BOTTOM), sweeping down/up and
// reversing at the ends like the original single bar did, but each now
// additionally PULSES in thickness (thin <-> thick, simulating a tube
// rotating edge-on to face-on) and cycles which of a small palette's
// colours occupies which row within its own current thickness (so the
// colour bands appear to flow/rotate too) -- both driven by the SAME
// per-bar rotation angle, advancing independently for each bar. Bar A
// starts at the top sweeping down ("behind"/translucent, ink stays
// white); bar B starts at the bottom sweeping up ("in front"/solid,
// ink=paper), with its own angle started 128 (half the 256-unit circle)
// ahead of bar A's -- they travel toward each other, cross near the
// middle, continue to the opposite ends, and repeat, each pulsing/
// colour-cycling out of sync with the other. Inspired by a real Oric
// demo ("His Masters Rasters" by IRIS) and a reference credits-screen
// image (a pair of tapered, multi-banded 3D-look bars) the user shared;
// this session cannot watch video content directly (no frame-by-frame
// video tool available), so the animation (bands cycling over time, not
// just a static tapered shape) is inferred from general demoscene
// "rotating bar" technique and confirmed reasonable by the user, not
// observed directly.
//
// The two sweep directions still differ in how they combine with the
// logo's own white pixels underneath, matching the "behind" vs. "in
// front of" framing from the original single-bar design:
//   - Sweeping down: PAPER only changes to each row's current band
//     colour, ink stays the logo's own baseline white -- the bar reads
//     as sitting BEHIND the artwork (translucent), so the white strokes
//     stay fully visible against the coloured background.
//   - Sweeping up: BOTH ink and paper are set to the SAME colour per
//     row, filling the entire row solid -- the bar reads as sitting IN
//     FRONT of the artwork (opaque), briefly hiding the strokes in that
//     band rather than merely tinting them.
//
// NO horizontal taper/trapezoid shape (despite the reference image
// showing one) -- deliberately skipped after checking this asset's own
// geometry, not just for simplicity. A true taper needs per-row ink/
// paper control bytes at COMPUTED, VARYING column positions, not just
// the fixed columns 0/1 set_rows()/hires_row_colors() already use. The
// only fully-blank column-bytes anywhere in this picture are 0-1
// (already claimed by the baseline ink/paper columns) and 38-39 (12px,
// right side only, no left-side counterpart) -- real wordmark content
// spans columns 2-37. Any left-side taper bracket would have to land on
// column 2, which is only 1/3 blank (px12-14; px15-17 is real letter-
// stroke content) -- placing a control byte there PERMANENTLY replaces
// that column's remaining pixel content, on every row, every time any
// bar's outer edge sweeps through, for the rest of the demo's run. A
// true symmetric taper isn't achievable with this asset's actual
// margins, independent of implementation care. Even a one-sided
// (right-only) taper adds a second hazard: set_rows()'s erase path only
// ever rewrites columns 0/1, so a bar-colour control byte written into
// normally-blank column 38-39 would never get restored to a blank pixel
// byte when the bar moves off that row -- it would permanently keep
// showing whatever colour last passed over it. The thickness pulse +
// colour-band cycling above already read as "3D" without needing this;
// a real taper, if wanted later, needs either a redrawn logo asset with
// deliberate margin (tools/oric_pictconv.py) or pixel-based drawing with
// a backup/restore buffer for the swept region -- both a separate,
// later task, not part of this one.
//
// Deliberately NOT built on include/rasterirq.h's hrirq_add() mid-frame
// IRQ callbacks: HIRES attribute bytes are embedded VRAM data the ULA
// reads continuously as it scans every frame, so a byte written well
// ahead of time (a plain hires_row_colors() call, same technique
// section_background.c's own colour bands use) looks IDENTICAL to one
// written via precisely-timed IRQ, as long as it holds the right value
// by the time the beam gets there -- true mid-frame IRQ timing only
// matters when the SAME byte position needs to show MULTIPLE different
// values within a single frame (impossible via a static write), which
// this effect never needs: each bar's position only changes once per
// tick, not multiple times within one 20ms frame. Genuine mid-frame
// raster-IRQ timing is reserved for the later standalone raster-IRQ
// showcase (a different section), where it actually is necessary.
//
// section_logo_tick() is `void`, never calling section_common.h's
// section_mark_finished() (the bars circle indefinitely, no natural end)
// -- see that header's own comment for why every section's tick() is void
// rather than returning bool.
//
// A THIRD investigation session (2026-07-12) found and fixed a real
// Oscar64 -O2 INLINER bug that made ANY per-row colour-varying bar design
// reproducibly hang the whole program (not Arkos, not a bird-sprite bug,
// not a caller-save-set defect) -- see set_rows()'s own __noinline
// comment below for the full mechanism, and ~/.claude/oscar64.md's
// "Whole-program register allocator can silently break an UNRELATED
// __interrupt handler..." entry (RESOLVED update) for the complete
// evidence trail. Any new per-row-varying loop in this file (like
// paint_bar() below) MUST be verified with a long Phosphoric soak test
// (hundreds of millions to 1+ billion cycles, both build targets,
// checking CPU register/PC state stays healthy and DIFFERENT across
// multiple increasing cycle-count samples, not frozen) before being
// trusted -- a short test or a single RAM dump is NOT sufficient.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "fixedmath.h"
#include "section_logo.h"

#ifdef STORAGE_FLOPPY
#define LOGO_FILE 2
#else
#define LOGO_FILE "oriclogo.bin"
#endif

// Row range the logo occupies within the loaded 240x200 picture (see this
// file's own header comment on how assets/oriclogo.bin was composited --
// resized to 210px wide, placed at x=15/y=45, so this spans y=45..154) --
// both bars circle within exactly this range.
#define LOGO_TOP     45u
#define LOGO_BOTTOM  154u
#define BAR_SPEED      2u

// Thickness pulses between BAR_MIN_THICK and BAR_MIN_THICK+BAR_THICK_RANGE-1
// rows (4..11) as a bar's own rotation angle advances -- thin ("edge-on")
// to thick ("face-on") and back, simulating a rotating tube. Comfortably
// under the logo's own 110-row travel range so a thick bar never
// dominates the whole picture.
#define BAR_MIN_THICK      4u
#define BAR_THICK_RANGE    8u

// BAR_PALETTE_SIZE == BAR_MIN_THICK is deliberate: at a bar's thinnest
// moment, exactly one clean, non-repeating pass through the palette is
// visible; at its thickest, roughly 2-3 repeats, giving a busier banded
// look while thick. A power of 2 so band selection is a cheap bitwise
// AND, no modulo/division needed.
#define BAR_PALETTE_SIZE   4u

// Ticks between rotation-angle steps -- ~85 ticks (~6s) per full 256-unit
// rotation at BAR_ANGLE_STEP=3, independent of each bar's own vertical
// sweep speed (BAR_SPEED).
#define BAR_ANGLE_STEP     3u

// Bar B's rotation starts half the 256-unit circle ahead of bar A's, so
// the two bars pulse/colour-cycle out of sync rather than in lockstep.
#define BAR_B_ANGLE_OFFSET 128u

typedef enum
{
    BAR_PHASE_DOWN,   // sweeping down, paper-only highlight ("behind")
    BAR_PHASE_UP      // sweeping up, solid ink+paper highlight ("in front")
} BarPhase;

typedef struct
{
    uint8_t  y;          // top row of this bar's CURRENT footprint
    uint8_t  thickness;  // row-count of that footprint (pulses over time)
    uint8_t  angle;      // rotation angle, 0..255, wraps naturally on overflow
    BarPhase phase;      // sweep direction
} BarState;

static BarState bar_a;
static BarState bar_b;

// Bar A: light/blue/cyan, echoing the reference image's top bar. Bar B:
// mostly blue with a red band, echoing its bottom bar. A_BGWHITE stands
// in for the reference's light-gray band -- the Oric's serial attributes
// have no true gray, only the 8 base colours in oric.h.
static const uint8_t bar_a_palette[BAR_PALETTE_SIZE] = { A_BGWHITE, A_BGBLUE, A_BGBLUE, A_BGCYAN };
static const uint8_t bar_b_palette[BAR_PALETTE_SIZE] = { A_BGBLUE,  A_BGBLUE, A_BGBLUE, A_BGRED  };

// __noinline: forces every call site through ONE standalone, correctly-
// compiled function body instead of letting Oscar64 inline this loop
// directly into a caller. Root cause of this whole file's long-running
// "paint_bar() hangs the whole program" investigation (see this file's
// own header comment above): a genuine Oscar64 INLINER bug, not
// Arkos, not a bird-sprite bug, not a caller-save-set defect. When
// Oscar64 inlines this loop into a call site with compile-time-constant
// ink/paper arguments, the inlined copy of the hires_row_off[y]-indexing
// arithmetic OMITS a high-byte reset between loop iterations that the
// STANDALONE, JSR'd version of this exact function correctly includes --
// confirmed via a side-by-side disassembly diff (the standalone function
// has "LDA #$00 / STA T0+1" right after each loop-counter increment; the
// inlined copy omits it). Each iteration after the first then computes
// the hires_row_off[] table LOOKUP ADDRESS using a STALE high byte left
// over from the PREVIOUS iteration's own already-computed HIRESVRAM
// destination, silently misreading an arbitrary on-screen pixel/
// attribute byte as a row offset and deriving a bogus write destination
// from it -- landing back in valid VRAM most of the time by chance, but
// occasionally wrapping into low memory and once, during the
// investigation, corrupting Arkos's own compiled code, eventually
// hanging the whole program tens of seconds later. See
// ~/.claude/oscar64.md's "Whole-program register allocator can silently
// break an UNRELATED __interrupt handler..." entry (RESOLVED update) for
// the full evidence trail.
__noinline static void set_rows(uint8_t y0, uint8_t h, uint8_t ink, uint8_t paper)
{
    uint8_t y;
    for (y = y0; y < (uint8_t)(y0 + h); y++)
        hires_row_colors(y, ink, paper);
}

// Thickness for a given rotation angle: oric_cos() ranges -127..127;
// +128 maps that to 1..255 (fits uint8_t, no wraparound), then a plain
// >>8 scale (BAR_THICK_RANGE is a power of 2) maps that to 0..7, plus
// BAR_MIN_THICK gives the final 4..11 row range. No loop, no
// hires_row_off touch, and its argument is always a genuinely
// runtime-varying struct field, never a compile-time constant at either
// call site -- outside the __noinline-requiring shape above, so this is
// deliberately left un-annotated; let the compiler decide.
static uint8_t bar_thickness_for_angle(uint8_t angle)
{
    uint8_t c = (uint8_t)(oric_cos(angle) + 128);
    return (uint8_t)(BAR_MIN_THICK + (((uint16_t)c * BAR_THICK_RANGE) >> 8));
}

// Paints one bar's current footprint, one row at a time, each row's
// colour chosen from palette[] by (row-within-bar + a rotation-derived
// band offset), wrapping via a bitwise AND since BAR_PALETTE_SIZE is a
// power of 2 -- no modulo/division needed. solid=false (BAR_PHASE_DOWN,
// "behind") keeps ink at the logo's own baseline white; solid=true
// (BAR_PHASE_UP, "in front") sets ink to the SAME colour as paper,
// filling the row fully solid. __noinline: this loop indexes
// hires_row_off[y] every iteration -- the exact shape set_rows()'s own
// comment above documents as unsafe to inline; see there for the full
// mechanism.
__noinline static void paint_bar(const BarState *bar, bool solid, const uint8_t *palette)
{
    uint8_t i;
    uint8_t band_phase = (uint8_t)(bar->angle >> 3);
    for (i = 0; i < bar->thickness; i++)
    {
        uint8_t paper = palette[(uint8_t)((i + band_phase) & (BAR_PALETTE_SIZE - 1u))];
        // palette[] holds A_BG* (paper-range, 16-23) values -- ink needs
        // the corresponding A_FW* (0-7) code, not the raw paper byte.
        // Passing the paper-range value straight into the ink slot writes
        // ANOTHER paper-style control byte at column 0, so ink silently
        // never changes (stays whatever the row above left behind) --
        // real bug found via screenshot: letters stayed visible through
        // the "solid" phase because ink was never actually set.
        uint8_t ink = solid ? (uint8_t)(paper - 16u) : A_FWWHITE;
        hires_row_colors((uint8_t)(bar->y + i), ink, paper);
    }
}

// Advances one bar's rotation angle/thickness and sweep position/phase
// by one tick -- no drawing, no hires_row_off touch at all, just struct-
// field arithmetic. __noinline: both call sites pass a compile-time-
// constant pointer (the fixed address of a static global), the same
// general "constant call-site argument" category that enabled the
// set_rows()/paint_bar() bug above (there, a constant scalar; here, a
// constant address) -- not a proven trigger for this specific function
// (it has no loop), but cheap defense-in-depth given __noinline costs an
// unmeasurable few cycles for a function called twice per tick.
//
// max_y is recomputed every tick from the bar's OWN, just-updated
// thickness, so the exact row where a bar reverses direction wobbles by
// a few rows lap to lap as thickness pulses -- an intended, harmless
// side effect of tying the sweep boundary to live thickness (organic
// variation), not a bug; do not "fix" this to a fixed reversal row.
__noinline static void bar_advance(BarState *bar)
{
    uint8_t max_y;

    bar->angle     = (uint8_t)(bar->angle + BAR_ANGLE_STEP);
    bar->thickness = bar_thickness_for_angle(bar->angle);
    max_y          = (uint8_t)(LOGO_BOTTOM - bar->thickness + 1u);

    if (bar->phase == BAR_PHASE_DOWN)
    {
        if (bar->y + BAR_SPEED > max_y)
        {
            bar->y = max_y;
            bar->phase = BAR_PHASE_UP;
        }
        else
            bar->y = (uint8_t)(bar->y + BAR_SPEED);
    }
    else
    {
        if (bar->y < (uint8_t)(LOGO_TOP + BAR_SPEED))
        {
            bar->y = LOGO_TOP;
            bar->phase = BAR_PHASE_DOWN;
        }
        else
            bar->y = (uint8_t)(bar->y - BAR_SPEED);
    }
}

void section_logo_init(const HiresBitmap *screen)
{
    (void)screen;

    // picture_load() writes straight into HIRESVRAM (screen->data IS
    // HIRESVRAM for the live canvas) -- no separate scratch buffer or
    // blit step needed, matching arkos_load()'s own "load straight into
    // its fixed destination" convention. Silently leaves the previous
    // section's own final frame on screen if this fails (no LOCI/floppy
    // device, file missing) rather than crashing, same graceful-failure
    // posture as arkos_load().
    picture_load(LOGO_FILE, (void *)HIRESVRAM, 8000);

    // Bar A: starts at the top, sweeping down ("behind"/translucent).
    bar_a.angle     = 0;
    bar_a.thickness = bar_thickness_for_angle(bar_a.angle);
    bar_a.y         = LOGO_TOP;
    bar_a.phase     = BAR_PHASE_DOWN;

    // Bar B: starts at the bottom, sweeping up ("in front"/solid), its
    // own rotation half a circle ahead of bar A's so they visibly
    // desynchronize rather than pulse/cycle in lockstep.
    bar_b.angle     = BAR_B_ANGLE_OFFSET;
    bar_b.thickness = bar_thickness_for_angle(bar_b.angle);
    bar_b.y         = (uint8_t)(LOGO_BOTTOM - bar_b.thickness + 1u);
    bar_b.phase     = BAR_PHASE_UP;

    paint_bar(&bar_a, bar_a.phase == BAR_PHASE_UP, bar_a_palette);
    paint_bar(&bar_b, bar_b.phase == BAR_PHASE_UP, bar_b_palette);
}

void section_logo_tick(const HiresBitmap *screen)
{
    (void)screen;

    // Pass 1: erase BOTH bars' OLD footprints first. Order between the
    // two calls doesn't matter -- writing the fixed baseline colour is
    // idempotent, so even if their old footprints overlapped, erasing
    // both just writes the same baseline twice on the shared rows.
    set_rows(bar_a.y, bar_a.thickness, A_FWWHITE, A_BGBLACK);
    set_rows(bar_b.y, bar_b.thickness, A_FWWHITE, A_BGBLACK);

    // Pass 2: advance state only -- no VRAM writes in this pass.
    bar_advance(&bar_a);
    bar_advance(&bar_b);

    // Pass 3: paint BOTH bars' NEW footprints, fixed order (A then B).
    // This ordering matters, not just for tidiness: doing erase/advance/
    // paint fully sequentially per bar could have bar B's erase (still
    // targeting B's OLD footprint) stomp on rows bar A just painted at
    // A's NEW position, if the two footprints happen to overlap that
    // tick. The 3-pass structure above avoids that entirely. On the rare
    // tick where the two bars' NEW footprints overlap (the actual
    // crossing moment), bar B's paint simply overwrites bar A's for
    // those shared rows -- a momentary, purely cosmetic artifact lasting
    // at most a couple of ticks per crossing, not a correctness issue.
    paint_bar(&bar_a, bar_a.phase == BAR_PHASE_UP, bar_a_palette);
    paint_bar(&bar_b, bar_b.phase == BAR_PHASE_UP, bar_b_palette);

    // circle indefinitely -- never calls section_mark_finished()
}
