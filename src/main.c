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
#include "pt3.h"
#include "rasterirq.h"
#include "section_background.h"
#include "section_bird.h"
#include "section_clouds.h"

// Background music: assets/oxygene4.pt3, from 6502Nerd/dflat's own
// tunes/ collection (the same MIT-licensed repo this project's PT3 player
// itself is ported from -- see docs/pt3.md's Attribution section). A
// chiptune cover of Jean-Michel Jarre's "Oxygene IV" -- used deliberately,
// same low-risk-in-practice demoscene convention as the rest of that
// tunes/ collection (nearly all covers of commercial tracks).
//
// pt3_load()'s signature (and this file reference) differs by target --
// see pt3.h/docs/pt3.md -- not a bug, a real, intentional difference:
//   - Tape/LOCI target: pt3_load(const char *path), loaded via LOCI at
//     runtime. Ships alongside build/oricdemo.tap in the USB/zip
//     distribution (Makefile's usb/zip targets), not embedded in the tape
//     binary itself.
//   - Floppy target (-dSTORAGE_FLOPPY): pt3_load(uint8_t file_index),
//     baked into the disk image at a fixed file-index slot (see
//     tools/floppy/disk_script.txt).
#ifdef STORAGE_FLOPPY
#define MUSIC_FILE 1
#else
#define MUSIC_FILE "oxygene4.pt3"
#endif

int main(void)
{
    hires_init();

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

    // Background music, ticking at 50Hz via a raster IRQ (see docs/pt3.md
    // and docs/rasterirq.md) -- decoupled from section_bird_run()'s own
    // busy-wait animation timing entirely; once started here it keeps
    // playing regardless of what any section does afterward. Silently
    // does nothing if the music file isn't present (no LOCI device, or
    // running a build where it wasn't shipped) -- see pt3_load()'s own
    // graceful-failure behaviour.
    if (pt3_load(MUSIC_FILE))
    {
        pt3_init();
        hrirq_init();
        hrirq_add(100, pt3_tick);
        hrirq_start();
    }

    section_clouds_init(&screen);
    section_bird_init(&screen);

    // Master loop: each section owns its own state and pacing, called in
    // turn every tick (see section_bird.h/section_clouds.h/
    // section_background.h). PT3 playback stays fully decoupled from
    // this, ticking via its own raster IRQ.
    for (;;)
    {
        section_background_tick(&screen);
        section_clouds_tick(&screen);
        section_bird_tick(&screen);
    }

    return 0;
}
