// main.c - oricdemo2026 entry point (real demo, HIRES mode)
//
// Sequences the demo's sections, each living in its own src/section_*.c
// file (Oscar64 #pragma compile chain -- see section_bird.h). Built with
// the alternate include/oric_crt_hires.c runtime, not the default
// include/oric_crt.c -- see include/hires.h's header comment for why HIRES
// bitmap graphics need that runtime's shrunk memory layout. The old
// TEXT-mode build-chain smoke test that used to live here moved to
// src/buildtest.c (see make test).

#include <string.h>
#include "oric.h"
#include "hires.h"
#include "arkos.h"
#include "rasterirq.h"
#include "section_background.h"
#include "section_bird.h"
#include "section_clouds.h"

// Background music: assets/steppingout.aky ("Mr.Lou - Dewfall Productions -
// Stepping out (2019)"), an Arkos Tracker module exported to $C000 -- see
// docs/arkos.md for the full format writeup, why this player replaced the
// earlier PT3 (Vortex Tracker) one (archived on the `pt3` branch after
// several rounds of decode-bug fixes still didn't produce satisfying music,
// and its runtime overhead was judged too high), and why Arkos modules load
// into overlay/floppy-target RAM at a fixed address rather than this
// project's own main code/data/BSS budget.
//
// arkos_load()'s signature (and this file reference) differs by target --
// see arkos.h/docs/arkos.md -- not a bug, a real, intentional difference:
//   - Tape/LOCI target: arkos_load(const char *path), loaded via LOCI at
//     runtime into overlay RAM ($C000-$F9FF). Ships alongside
//     build/oricdemo.tap in the USB/zip distribution (Makefile's usb/zip
//     targets), not embedded in the tape binary itself.
//   - Floppy target (-dSTORAGE_FLOPPY): arkos_load(uint8_t file_index),
//     baked into the disk image at a fixed file-index slot (see
//     tools/floppy/disk_script.txt), loaded straight into that target's
//     already-plain RAM at the same $C000 address.
#ifdef STORAGE_FLOPPY
#define MUSIC_FILE 1
#else
#define MUSIC_FILE "steppingout.aky"
#endif

// Real frame-pacing for the master loop below, replacing what used to be
// an uncontrolled busy-loop (each section's own tick function ran back to
// back, as fast as the CPU happened to get through them -- no two visual
// updates took the same real amount of time, since section_bird.c's own
// per-tick cost varies with what the bird happens to be doing that tick).
// Reuses rasterirq.h's already-proven, already-calibrated 50Hz Timer 1 IRQ
// (oric.h's TIMER1_50HZ "matches ART11's own measured cycles/frame" --
// i.e. this really is locked to the real video frame rate, not an
// arbitrary clock) rather than inventing a second timing mechanism: one
// more tiny callback alongside arkos_tick's own, just counting ticks.
static volatile uint8_t main_frame_tick;

__interrupt void main_frame_tick_isr(void)
{
    main_frame_tick++;
}

// How many real 50Hz ticks the master loop waits out per visual update.
// Chosen from real measurements (a temporary debug tick-counter, read back
// via Phosphoric RAM dumps -- same technique used throughout this
// project's own performance/decode investigations): the UNPACED loop took
// ~151ms/iteration before section_background.c's precomputed tree
// brackets, section_bird.c's original bird_prepare_frame() fast path, and
// its BIRD_TICK_EVERY gate -- with those in place, ~71ms. Confining the
// bird to a background-free sky band (see section_bird.c's own header
// comment) removed the tree-overlap scan entirely and switched its
// draw/erase to HXSPR_XOR with no backup buffer at all, dropping the
// floor further, to ~50ms. 3 ticks (60ms, 16.7Hz) sits just above that
// measured floor, giving tight, consistent pacing (~74-77ms measured,
// most of that the deliberate wait, down from the previous setting's
// ~100-103ms) with only modest headroom to spare -- raise this if a
// future change adds enough per-tick cost to make 60ms too tight (the
// loop itself stays correct either way: see its own comment, an
// iteration whose real work exceeds the target is never slowed down
// further).
#define MAIN_FRAME_PACING_TICKS 3u

int main(void)
{
    hires_init();

    // Background music, started FIRST -- before hb_init/hb_fill/hires_on/
    // section_background_run below -- so it's audible from the very start
    // of the demo, not just once the (visually much slower) background/
    // footer setup below has already finished. Ticks at 50Hz via a raster
    // IRQ (see docs/arkos.md and docs/rasterirq.md) -- entirely decoupled
    // from every section's own animation timing; once started here it
    // keeps playing regardless of what any section does afterward, for
    // the demo's entire run. Silently does nothing if the music file
    // isn't present (no LOCI device, or running a build where it wasn't
    // shipped) -- see arkos_load()'s own graceful-failure behaviour. Has
    // no dependency on the HIRES video setup below (arkos.c/rasterirq.c
    // only ever touch the AY/VIA registers, never HIRESVRAM), so nothing
    // is lost by moving it ahead of it.
    //
    // hrirq_init()/hrirq_add(main_frame_tick_isr)/hrirq_start() run
    // UNCONDITIONALLY now (previously only inside the arkos_load() branch,
    // since only music needed them) -- the master loop's own frame-pacing
    // below needs a working 50Hz tick regardless of whether music loaded,
    // so interrupts get enabled even in the no-music case now. arkos_tick
    // itself is still only registered when arkos_load() actually succeeds.
    hrirq_init();
    if (arkos_load(MUSIC_FILE))
    {
        arkos_init();
        hrirq_add(100, arkos_tick);
    }
    hrirq_add(20, main_frame_tick_isr);
    hrirq_start();

    HiresBitmap screen;
    hb_init(&screen, (uint8_t *)HIRESVRAM, HIRES_ROWS);
    hb_fill(&screen, 0x40);   // real RAM isn't zero-initialized -- start blank

    hires_on(true);

    // Draws the sky/bank/river background AND establishes a known
    // white-ink baseline for every row (varying only PAPER by band) --
    // sections that colour their own sprites (see section_bird.c's
    // HxsprColor use) rely on ink being fixed/predictable to restore to,
    // since ink/paper attributes cascade rightward from wherever they were
    // last set (see hires.h). Must run before section_bird_run() draws on
    // top of it.
    section_background_run(&screen);

    // Deliberately NOT calling hires_footer_enable() here: its 3-row TEXT
    // footer reads glyph data from $B400/$B800 (see that function's own
    // doc comment in hires.h for the full story) -- addresses that alias
    // directly with this demo's own HIRES rows ~128-199 (the creek, and
    // wherever the bird currently is). Enabling it would render footer
    // "blank" characters as whatever pixel/attribute bytes are currently
    // sitting in that part of the picture -- reported as "corruption in
    // the lower border" running the real demo in Oricutron.
    //
    // The bottom 24 scanlines (3 TEXT rows, $BF68-$BFDF) still need
    // explicit setup though, hires_footer_enable() or not: they render as
    // TEXT mode UNCONDITIONALLY on real hardware (HIRES bitmap only ever
    // covers 200 scanlines -- see hires_on()'s own doc comment), so
    // whatever's sitting there gets interpreted as character codes and
    // rendered against whatever charset is active (which hires_on()
    // already made blank). CH_SPACE for the character codes themselves,
    // plus an explicit white-ink/black-paper attribute pair at the start
    // of each of the 3 rows (real Oric RAM isn't zero-initialized --
    // relying on the ULA's per-scanline reset alone isn't robust against
    // stray attribute bytes already sitting there from power-on).
    {
        uint8_t *footer = (uint8_t *)HIRES_FOOTER;
        uint8_t row;
        for (row = 0; row < HIRES_FOOTER_ROWS; row++)
        {
            uint8_t *p = footer + (uint16_t)row * HIRES_ROW_BYTES;
            p[0] = A_FWWHITE;
            p[1] = A_BGBLACK;
            memset(p + 2, CH_SPACE, HIRES_ROW_BYTES - 2);
        }
    }

    section_clouds_init(&screen);
    section_bird_init(&screen);

    // Master loop: each section owns its own state and pacing, called in
    // turn every tick (see section_bird.h/section_clouds.h/
    // section_background.h). Arkos playback stays fully decoupled from
    // this, ticking via its own raster IRQ.
    //
    // The wait at the bottom paces each iteration to a fixed
    // MAIN_FRAME_PACING_TICKS real 50Hz ticks (see that constant's own
    // comment) -- (uint8_t)(main_frame_tick - start_tick) rather than a
    // plain equality/greater-than check so it's correct across the
    // counter's own 0-255 wraparound, and so an iteration whose real work
    // already took longer than the target falls through immediately
    // rather than waiting an extra ~256-tick wraparound.
    for (;;)
    {
        uint8_t start_tick = main_frame_tick;

        section_background_tick(&screen);
        section_clouds_tick(&screen);
        section_bird_tick(&screen);

        while ((uint8_t)(main_frame_tick - start_tick) < MAIN_FRAME_PACING_TICKS)
            ;
    }

    return 0;
}
