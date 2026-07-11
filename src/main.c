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
#include "keyboard.h"
#include "section_background.h"
#include "section_bird.h"
#include "section_clouds.h"
#include "section_splash.h"
#include "rom_charset.h"
#include "idi8b_altcharset.h"

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

// A demo "section" -- one entry per part of the running order (idi8b
// splash, logo/raster-bar intro, the bird scene, each showcase, credits
// -- see docs/README.md or the project's own planning notes for the full
// list; only the bird scene exists so far, everything else lands in
// later phases). `init` is called once, `tick` every main-loop iteration
// thereafter, both given the same shared `screen` canvas every section
// uses. `min_ticks`/`max_ticks` count main-loop iterations (NOT raw 50Hz
// raster ticks -- see MAIN_FRAME_PACING_TICKS, each iteration already
// spans several of those): keypresses are ignored before `min_ticks` (so
// a section is never skipped before it's even had a chance to show
// anything), and the section is force-advanced at `max_ticks` even with
// no keypress at all (so a section can never hang the demo forever).
// `tick` returns true once the section has naturally finished on its own
// (e.g. section_splash.c's fade-out completing) -- run_section() below
// advances immediately when that happens, regardless of min_ticks/
// max_ticks; a section with no natural end (the bird scene) simply
// always returns false.
typedef struct
{
    void (*init)(const HiresBitmap *screen);
    bool (*tick)(const HiresBitmap *screen);
    uint16_t min_ticks;
    uint16_t max_ticks;
} DemoSection;

// Runs one section to completion: its own init, then tick+pace every
// iteration until the section itself signals it's finished, a keypress
// lands (once min_ticks has elapsed), or max_ticks is reached regardless.
// keyb_check() is safe to call here even with hrirq_start() already
// active -- keyboard.c's own keyb_scan() already brackets its VIA/AY
// access with php/sei...plp internally (see that file), the exact
// convention rasterirq.h's own header comment requires of anything
// touching VIA Port A while interrupts are live.
static void run_section(const DemoSection *section, const HiresBitmap *screen)
{
    uint16_t elapsed = 0;

    section->init(screen);
    for (;;)
    {
        uint8_t start_tick = main_frame_tick;
        bool finished = section->tick(screen);

        while ((uint8_t)(main_frame_tick - start_tick) < MAIN_FRAME_PACING_TICKS)
            ;

        elapsed++;
        if (finished)
            return;
        if (elapsed >= section->max_ticks)
            return;
        if (elapsed >= section->min_ticks && keyb_check())
            return;
    }
}

// Wraps the existing background/clouds/bird trio as a single section --
// section_background_run() doubles as this section's own init (it was
// always a one-shot draw, not persistent per-section state), same
// ordering as before this refactor (background, then the TEXT footer,
// then clouds, then bird). min_ticks/max_ticks are both set to
// (effectively) "never" for now -- there is no other section yet for
// this one to advance into, so this phase is a pure structural refactor,
// not a behaviour change; real pacing numbers land once section #4
// (the first showcase section) actually exists to advance into.
#define SECTION_FOREVER 0xFFFFu

static void bird_scene_init(const HiresBitmap *screen)
{
    section_background_run(screen);
    section_clouds_init(screen);
    section_bird_init(screen);
}

static bool bird_scene_tick(const HiresBitmap *screen)
{
    section_background_tick(screen);
    section_clouds_tick(screen);
    section_bird_tick(screen);
    return false;   // no natural end -- runs until skipped or timed out
}

// idi8b splash: min_ticks keeps an impatient keypress from insta-skipping
// before the logo has had a moment to render; max_ticks (~37s) is purely
// a safety backstop -- the section almost always finishes on its own (its
// own tick() returns true once the fade-out completes) well before this
// would ever fire. Now a normal HIRES-mode section like any other (it
// used to run outside the sections[] table, before hires_on() -- see git
// history -- back when it was TEXT-mode content; moving it to HIRES mode
// removed that whole special case).
#define SPLASH_MIN_TICKS 20u
#define SPLASH_MAX_TICKS 500u

// The demo's own running order -- currently the idi8b splash followed by
// the one existing scene; later phases insert the logo-intro between them
// and the showcase sections/credits after (see this project's own
// planning notes for the full list).
static const DemoSection sections[] = {
    { section_splash_init, section_splash_tick, SPLASH_MIN_TICKS, SPLASH_MAX_TICKS },
    { bird_scene_init, bird_scene_tick, SECTION_FOREVER, SECTION_FOREVER },
};
#define NUM_SECTIONS (sizeof(sections) / sizeof(sections[0]))

int main(void)
{
    // Background music, started FIRST -- before the idi8b splash below,
    // and before hb_init/hb_fill/hires_on/section_background_run further
    // down -- so it's audible from the very start of the demo, not just
    // once the (visually much slower) splash/background/footer setup has
    // already finished. Ticks at 50Hz via a raster IRQ (see docs/arkos.md
    // and docs/rasterirq.md) -- entirely decoupled from every section's
    // own animation timing; once started here it keeps playing regardless
    // of what any section does afterward, for the demo's entire run.
    // Silently does nothing if the music file isn't present (no LOCI
    // device, or running a build where it wasn't shipped) -- see
    // arkos_load()'s own graceful-failure behaviour. Has no dependency on
    // TEXT or HIRES video state (arkos.c/rasterirq.c only ever touch the
    // AY/VIA registers, never TEXTVRAM/HIRESVRAM), so nothing is lost by
    // starting it this early.
    //
    // hrirq_init()/hrirq_add(main_frame_tick_isr)/hrirq_start() run
    // UNCONDITIONALLY now (previously only inside the arkos_load() branch,
    // since only music needed them) -- run_section()'s own frame-pacing
    // needs a working 50Hz tick regardless of whether music loaded, so
    // interrupts get enabled even in the no-music case now. arkos_tick
    // itself is still only registered when arkos_load() actually succeeds.
    hrirq_init();
    if (arkos_load(MUSIC_FILE))
    {
        arkos_init();
        hrirq_add(100, arkos_tick);
    }
    hrirq_add(20, main_frame_tick_isr);
    hrirq_start();

    hires_init();

    HiresBitmap screen;
    hb_init(&screen, (uint8_t *)HIRESVRAM, HIRES_ROWS);
    hb_fill(&screen, 0x40);   // real RAM isn't zero-initialized -- start blank

    hires_on(true);

    // Copy real charset data into HIRES_CHARSET_STD/ALT, right after
    // hires_on() (which zeroes both, see hires.c's own comment) and
    // before any section runs -- "charsets copied to RAM at boot", once,
    // for the whole program's runtime, rather than any section reading
    // charset data live from a source that isn't reliably valid (see
    // hires.c's hb_put_chars() and assets/rom_charset.h's own comments
    // for exactly why CHARSETROM/CHARSET_ALT aren't safe to read directly
    // on either of this project's targets).
    //   - HIRES_CHARSET_STD: the real Oric ROM's own standard 6x8 font
    //     (assets/rom_charset.h), so hb_put_chars() (any section, not
    //     just the splash) always has real glyph data to read. rom_charset
    //     only holds the 96 PRINTABLE glyphs (CHARSETROM's own 0-based
    //     convention), but HIRES_CHARSET_STD is a full 128-entry table
    //     indexed from code 0 (hb_put_chars()'s own `ch*8` addressing,
    //     matching real charset RAM layout) -- copied at offset 0x20*8
    //     (0x100), not offset 0, leaving codes 0x00-0x1F as hires_on()'s
    //     own zero-fill. Getting this offset wrong once already produced a
    //     real, visible bug: every glyph shifted by 0x100 bytes, making
    //     the footer's own blank CH_SPACE cells render some other, wrong,
    //     non-blank shape instead.
    //   - HIRES_CHARSET_ALT: only the 3 specific glyph codes
    //     section_splash.c's mosaic wordmark actually uses
    //     (assets/idi8b_altcharset.h), placed at their own code*8 offset
    //     -- the rest of the bank stays blank (hires_on()'s own zero-fill),
    //     since nothing else currently needs alt-charset content.
    memcpy((void *)(HIRES_CHARSET_STD + 0x20 * 8), rom_charset, sizeof(rom_charset));
    {
        uint8_t i;
        for (i = 0; i < 3; i++)
            memcpy((void *)(HIRES_CHARSET_ALT + (uint16_t)idi8b_altcharset[i].code * 8),
                   idi8b_altcharset[i].glyph, 8);
    }

    // The bottom 24 scanlines (3 TEXT rows, $BF68-$BFDF) need explicit
    // setup before ANY section runs, splash included: they render as TEXT
    // mode UNCONDITIONALLY on real hardware (HIRES bitmap only ever
    // covers 200 scanlines -- see hires_on()'s own doc comment), so
    // whatever's sitting there gets interpreted as character codes and
    // rendered against whatever charset is active. Previously done inside
    // bird_scene_init() (section #2) -- moved here since the splash
    // (section #1) showed this same uninitialized-footer garbage (a
    // solid white block) before that ever ran. Deliberately NOT calling
    // hires_footer_enable(): see that function's own doc comment in
    // hires.h for why its own footer mechanism aliases with this demo's
    // own HIRES content. CH_SPACE for the character codes themselves,
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

    // Runs the demo's own section table forever, one section at a time --
    // each section owns its own state/pacing internally (see
    // section_bird.h/section_clouds.h/section_background.h), run_section()
    // above owns advancing between them. Arkos playback stays fully
    // decoupled from all of this, ticking via its own raster IRQ
    // regardless of which section is currently showing.
    for (;;)
    {
        uint8_t i;
        for (i = 0; i < NUM_SECTIONS; i++)
            run_section(&sections[i], &screen);
    }

    return 0;
}
