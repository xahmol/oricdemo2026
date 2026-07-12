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
// reasoning as the Arkos music module).
//
// Raster bar: a single highlight band circles through the logo's own row
// range (LOGO_TOP..LOGO_BOTTOM), moving BAR_SPEED scanlines per main-loop
// tick -- sweeping down first, then, on reaching the bottom, reversing to
// sweep back up, reaching the top, and repeating indefinitely.
//
// The two sweep directions differ in how they combine with the logo's
// own white pixels underneath, matching the "behind" vs. "in front of"
// framing the user asked for:
//   - Sweeping down: PAPER only changes to the bar's colour, ink stays
//     the logo's own baseline white -- the bar reads as sitting BEHIND
//     the artwork (translucent), so the white strokes stay fully visible
//     against the coloured background.
//   - Sweeping up: BOTH ink and paper are set to the SAME colour, filling
//     the entire row solid -- the bar reads as sitting IN FRONT of the
//     artwork (opaque), briefly hiding the strokes in that band rather
//     than merely tinting them.
//
// Multi-colour bar: paint_bar() below gives each of the bar's rows its own
// PAPER colour from bar_pattern[]. Two earlier investigation sessions
// found that EVERY per-row colour-varying design reproducibly hung the
// entire program after anywhere from under a minute to tens of minutes of
// real playback -- but only with the Arkos Tracker music player
// (include/arkos.c) loaded and actively playing real song data -- and
// reverted to a single fixed colour rather than ship a hang. A THIRD
// session (2026-07-12) finally found and fixed the real root cause: a
// genuine Oscar64 INLINER bug, not Arkos, not a bird-sprite bug, not a
// caller-save-set defect (see set_rows()'s own __noinline comment below
// for the full mechanism). With that fix in place (__noinline on
// set_rows()), this file's own multi-colour paint_bar() has been verified
// clean over 100M/300M/500M/700M/1000M/1500M-cycle Phosphoric soak tests
// on BOTH build targets, healthy progressing PC/registers at every
// sample -- see ~/.claude/oscar64.md's "Whole-program register allocator
// can silently break an UNRELATED __interrupt handler..." entry
// (RESOLVED update) for the full evidence trail. Do not revert to a
// single fixed colour without first checking that entry -- this exact
// design is now confirmed safe.
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
// this effect never needs: the bar's position only changes once per
// tick, not multiple times within one 20ms frame. Genuine mid-frame
// raster-IRQ timing is reserved for the later standalone raster-IRQ
// showcase (a different section), where it actually is necessary.
//
// section_logo_tick() is `void`, never calling section_common.h's
// section_mark_finished() (the bar circles indefinitely, no natural end)
// -- see that header's own comment for why every section's tick() is void
// rather than returning bool.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "section_logo.h"

#ifdef STORAGE_FLOPPY
#define LOGO_FILE 2
#else
#define LOGO_FILE "oriclogo.bin"
#endif

// Row range the logo occupies within the loaded 240x200 picture (see this
// file's own header comment on how assets/oriclogo.bin was composited --
// resized to 210px wide, placed at x=15/y=45, so this spans y=45..154) --
// the bar circles within exactly this range.
#define LOGO_TOP     45u
#define LOGO_BOTTOM  154u
#define BAR_HEIGHT     6u
#define BAR_SPEED      2u
#define BAR_MAX_Y    (LOGO_BOTTOM - BAR_HEIGHT + 1u)

// Per-row PAPER palette for the bar, indexed by each row's fixed position
// within a single paint_bar() call -- a plain compile-time-constant
// array, no time-varying state. See this file's own header comment and
// set_rows()'s __noinline comment below for why this design is safe now
// (it was not, before the __noinline fix).
static const uint8_t bar_pattern[BAR_HEIGHT] = {
    A_BGYELLOW, A_BGCYAN, A_BGGREEN, A_BGMAGENTA, A_BGRED, A_BGBLUE
};

typedef enum
{
    BAR_PHASE_DOWN,   // sweeping down, paper-only highlight ("behind")
    BAR_PHASE_UP      // sweeping up, solid ink+paper highlight ("in front")
} BarPhase;

static uint8_t  bar_y;
static BarPhase bar_phase;

// __noinline: forces every call site through ONE standalone, correctly-
// compiled function body instead of letting Oscar64 inline this loop
// directly into a caller. Root cause of this whole file's long-running
// "paint_bar() hangs the whole program" investigation (see this file's
// own header comment above): a genuine Oscar64 INLINER bug, not
// Arkos, not a bird-sprite bug, not a caller-save-set defect. When
// Oscar64 inlines this loop into a call site with compile-time-constant
// ink/paper arguments (confirmed: section_logo_tick()'s own baseline-
// restore call, `set_rows(bar_y, BAR_HEIGHT, A_FWWHITE, A_BGBLACK)`), the
// inlined copy of the hires_row_off[y]-indexing arithmetic OMITS a
// high-byte reset between loop iterations that the STANDALONE, JSR'd
// version of this exact function correctly includes -- confirmed via a
// side-by-side disassembly diff (the standalone function has "LDA #$00 /
// STA T0+1" right after each loop-counter increment; the inlined copy
// omits it). Each iteration after the first then computes the
// hires_row_off[] table LOOKUP ADDRESS using a STALE high byte left over
// from the PREVIOUS iteration's own already-computed HIRESVRAM
// destination, silently misreading an arbitrary on-screen pixel/
// attribute byte as a row offset and deriving a bogus write destination
// from it -- landing back in valid VRAM most of the time by chance
// (explaining 1.5+ billion clean soak-test cycles on builds where the
// garbage stayed harmless), but occasionally wrapping into low memory
// when the misread on-screen byte happens to be large enough to overflow
// the "+ $A0" VRAM-base add past $FF. Confirmed via a cycle-exact
// Phosphoric trace: this exact bug derived destination address $091D
// (inside arkos_advance_pattern()'s own compiled code, include/arkos.c),
// overwrote 2 of its instruction bytes with the baseline ink/paper values
// ($07/$10 = A_FWWHITE/A_BGBLACK) being painted, and the next tick's
// arkos_advance_pattern() executed the corrupted bytes as a wrong
// instruction, eventually corrupting the 6502 hardware stack and hanging
// the whole program tens of seconds later -- explaining every earlier
// observation (needs a real interrupt firing, needs specific timing/
// on-screen content, corrupts an unrelated function's control flow, no
// static save-set gap). See ~/.claude/oscar64.md's "third register-
// allocator-class bug" entry for the two earlier, inconclusive
// investigation sessions this one finally resolved.
__noinline static void set_rows(uint8_t y0, uint8_t h, uint8_t ink, uint8_t paper)
{
    uint8_t y;
    for (y = y0; y < (uint8_t)(y0 + h); y++)
        hires_row_colors(y, ink, paper);
}

// Paints the bar with a DIFFERENT paper colour per row, chosen only by
// that row's fixed position within the bar (bar_pattern[] above).
static void paint_bar(uint8_t y0, uint8_t ink)
{
    uint8_t i;
    for (i = 0; i < BAR_HEIGHT; i++)
        hires_row_colors((uint8_t)(y0 + i), ink, bar_pattern[i]);
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

    bar_y = LOGO_TOP;
    bar_phase = BAR_PHASE_DOWN;
    paint_bar(bar_y, A_FWWHITE);
}

void section_logo_tick(const HiresBitmap *screen)
{
    (void)screen;

    // Restore the bar's current rows back to the picture's own baseline
    // (white ink, black paper -- the mono conversion's own colours)
    // before moving it.
    set_rows(bar_y, BAR_HEIGHT, A_FWWHITE, A_BGBLACK);

    if (bar_phase == BAR_PHASE_DOWN)
    {
        if (bar_y + BAR_SPEED > BAR_MAX_Y)
        {
            bar_y = BAR_MAX_Y;
            bar_phase = BAR_PHASE_UP;
        }
        else
            bar_y = (uint8_t)(bar_y + BAR_SPEED);
    }
    else
    {
        if (bar_y < (uint8_t)(LOGO_TOP + BAR_SPEED))
        {
            bar_y = LOGO_TOP;
            bar_phase = BAR_PHASE_DOWN;
        }
        else
            bar_y = (uint8_t)(bar_y - BAR_SPEED);
    }

    if (bar_phase == BAR_PHASE_DOWN)
        paint_bar(bar_y, A_FWWHITE);      // behind, per-row colour
    else
        paint_bar(bar_y, A_FWYELLOW);     // in front, per-row colour

    // circles indefinitely -- never calls section_mark_finished()
}
