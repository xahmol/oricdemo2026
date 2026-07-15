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
#include "section_logo.h"
#include "section_hires_showcase.h"
#include "section_polygon_workout.h"
#include "section_func3d.h"
#include "section_sprite_showcase.h"
#include "section_scroll_showcase.h"
#include "section_wave_showcase.h"
#include "section_dissolve_showcase.h"
#include "section_macaw_showcase.h"
#include "section_rasterirq_showcase.h"
#include "section_credits.h"
#include "section_common.h"
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
#define MUSIC_FILE  1
#define MUSIC_FILE2 3
#else
#define MUSIC_FILE  "steppingout.aky"
#define MUSIC_FILE2 "boulesetbits.aky"
#endif

// Alternates between the demo's two music tracks once the currently
// playing one finishes a full playthrough (arkos_song_finished(), see
// arkos.c's own "end of song" detection in arkos_advance_pattern()) --
// keeps a long-running demo from looping the exact same tune forever
// instead of only ever playing assets/steppingout.aky. Replaces an
// earlier design (a ONE-TIME hardcoded switch to boulesetbits.aky at the
// section_hires_showcase.c boundary) with a general, completion-driven
// toggle that runs for the whole demo -- see that file's own header
// comment for the full history. Same hrirq_stop()/arkos_stop()/
// arkos_load()/arkos_init()/hrirq_start() bracket as any other genuine
// track restart (NOT arkos_pause()/arkos_resume(), which is for a brief
// same-track pause only -- see docs/arkos.md's "Pause vs. stop" section).
static uint8_t current_track;   // 1 or 2, mirrors which of the two files above is loaded

static void music_check_toggle(void)
{
    if (arkos_song_finished())
    {
        hrirq_stop();
        arkos_stop();
        current_track = (current_track == 1) ? 2 : 1;
        if (arkos_load(current_track == 1 ? MUSIC_FILE : MUSIC_FILE2))
            arkos_init();
        hrirq_start();
    }
}

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
//
// `tick` is `void`, NOT `bool` -- a section that naturally finishes (e.g.
// section_splash.c's fade-out completing) calls section_mark_finished()
// (section_common.h) instead of returning a value; run_section() below
// checks/resets that flag once per iteration and advances immediately when
// it's set, regardless of min_ticks/max_ticks. A section with no natural
// end (the bird scene, the logo's indefinitely-circling raster bar) simply
// never calls it. See section_common.h's own header comment for exactly
// why this is `void` and not `bool`: an earlier `bool`-returning design,
// with "finished" read back out of a function pointer's return value, hit
// a real Oscar64 -O2 code-generation bug (a tick() implementation whose
// tail is [call a void helper][load/compute a value][RTS] with nothing
// else in between doesn't reliably get its return value stored anywhere
// durable) that a same-file sibling function's own unrelated stack-cleanup
// code happened to mask. An inline-asm workaround forcing that store was
// tried and confirmed UNSAFE in practice: it crashed the floppy target to
// Oricutron's monitor and hung Phosphoric outright (most likely by
// clobbering zero-page state something else nearby still needed) --
// reordering when run_section() read the value didn't help either, since
// the callee still never wrote it in the first place regardless of when
// the caller looked. Communicating "finished" via a plain function call
// instead sidesteps the whole bug class permanently: a void function's
// return path never goes through the compiler's return-value machinery at
// all, so there is nothing left for this bug (or any sibling of it) to
// corrupt.
typedef struct
{
    void (*init)(const HiresBitmap *screen);
    void (*tick)(const HiresBitmap *screen);
    uint16_t min_ticks;
    uint16_t max_ticks;
} DemoSection;

static bool section_finished_flag;

void section_mark_finished(void)
{
    section_finished_flag = true;
}

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

    section_finished_flag = false;
    section->init(screen);
    for (;;)
    {
        uint8_t start_tick = main_frame_tick;
        section->tick(screen);

        while ((uint8_t)(main_frame_tick - start_tick) < MAIN_FRAME_PACING_TICKS)
            ;

        music_check_toggle();

        elapsed++;
        if (section_finished_flag)
            return;
        if (elapsed >= section->max_ticks)
            return;
        if (elapsed >= section->min_ticks && keyb_check())
            return;
    }
}

// Wipes the just-finished section's content before the next section's own
// init runs, rather than letting one section's leftover pixels/attributes
// sit there until the next section happens to overdraw them (most _init()
// functions already hb_fill() a blank canvas themselves, but that's an
// INSTANT cut -- this gives every section boundary a visible transition
// instead). A left-to-right sweeping "curtain": each tick, blanks the next
// TRANSITION_COLS_PER_TICK column-bytes (all 200 rows), growing the wipe
// rightward until the whole 40-column canvas is blank.
//
// Deliberately a raw per-row memset(), NOT hb_rect_fill() (tried first,
// then measured via a real Phosphoric RAM-dump/PC-sample investigation --
// the CPU was found parked inside hb_put() for tens of millions of cycles,
// tens of REAL SECONDS, on just one section transition): hb_rect_fill()
// draws one pixel at a time via hb_put() -- correct for arbitrary
// shapes, but for a solid full-height column band that's HIRES_ROWS *
// (TRANSITION_COLS_PER_TICK*6) = 4800 individual hb_put() calls per tick,
// 48000 total for the full sweep, each far more expensive than a raw byte
// write once row-offset/bit-mask lookups are involved. A column band is
// always BYTE-aligned already (columns here are column-BYTES, not
// pixels -- HIRES_ROW_BYTES of them per row, 6px each), so memset()int
// straight into HiresBitmap's own data is both correct and the same
// "raw byte fill" hb_fill() itself already relies on for instant full-
// screen clears -- just scoped to one column band instead of the whole
// row. 0x40 is HIRES's own "blank" byte value (bit6 set, no pixel bits) --
// same convention hb_fill(screen, 0x40) and hb_set()/hb_clr() already use
// (see hires.c's own header comment on that bit).
//
// The sweep also blanks column-bytes 0-1 of every row -- the row's own
// ink/paper CONTROL bytes, not ordinary pixel data (see hires.h's own
// comment on why those two columns are special) -- exactly like every
// section's own hb_fill(screen, 0x40) already does before it calls
// hires_row_colors() to re-assert real values. hires_row_colors_range()
// below does exactly that re-assertion, once, after the sweep completes,
// resetting the whole screen to a plain white-ink/black-paper baseline so
// the next section's own init starts from a known-clean state.
#define TRANSITION_COLS_PER_TICK 4u
#define TRANSITION_TOTAL_COLS    (HIRES_ROW_BYTES)

// Blanks one column-band (all HIRES_ROWS rows) -- split out into its own
// small helper, rather than nested inside transition_clear() itself, to
// keep transition_clear()'s own live-local footprint small. This isn't
// just style: a real, previously-documented Oscar64 -O2 whole-program
// register-allocator bug (~/.claude/oscar64.md: "caller-save set can be
// under-counted") silently corrupted a DIFFERENT live zero-page value
// elsewhere when this logic was written as one larger function with more
// simultaneously-live locals (col/row/start_tick all in one frame) --
// confirmed via a real crash (Oricutron floppy target monitor-trapped,
// Phosphoric hung, both with the CPU found parked executing DATA bytes as
// instructions after a corrupted jump). Splitting the row-loop out into
// its own minimal-footprint function avoided it. Do not merge this back
// into transition_clear() without re-testing a full run all the way
// through a real section transition in the emulator.
static void transition_clear_band(uint8_t *data, uint8_t col)
{
    uint8_t row;
    for (row = 0; row < HIRES_ROWS; row++)
        memset(data + (uint16_t)row * HIRES_ROW_BYTES + col, 0x40, TRANSITION_COLS_PER_TICK);
}

static void transition_clear(const HiresBitmap *screen)
{
    uint8_t col;

    // Disarms section_rasterirq_showcase.c's own __interrupt callback
    // FIRST, before this sweep writes a single byte -- see that section's
    // own header comment for why an armed callback must not survive past
    // its own section (hrirq_add() has no "remove" primitive). A no-op
    // for every other section transition (the flag is already false).
    section_rasterirq_showcase_deactivate();

    for (col = 0; col < TRANSITION_TOTAL_COLS; col = (uint8_t)(col + TRANSITION_COLS_PER_TICK))
    {
        uint8_t start_tick = main_frame_tick;
        transition_clear_band(screen->data, col);
        while ((uint8_t)(main_frame_tick - start_tick) < MAIN_FRAME_PACING_TICKS)
            ;
        music_check_toggle();
    }

    // A plain loop, not dissolve.h's hires_row_colors_range() -- pulling in
    // that entire compilation unit for one trivial ranged loop isn't
    // justified (nothing else in this program uses dissolve.c), and this
    // project's own Oscar64 -O2 whole-program register allocator is
    // demonstrably sensitive to total program size in ways that can
    // silently corrupt unrelated code (~/.claude/oscar64.md's "caller-save
    // set can be under-counted" -- confirmed here too: pulling in
    // dissolve.c pushed a real Arkos Tracker __interrupt handler,
    // arkos_tick(), right up against Oscar64's own documented
    // "-O" -level-sensitive interrupt-complexity limit, silently
    // corrupting its behaviour rather than hard-erroring -- reverting this
    // one dependency was enough to fix it, confirmed via a real emulator
    // soak test). Keeping this inline avoids the whole risk category.
    {
        uint8_t y;
        for (y = 0; y < HIRES_ROWS; y++)
            hires_row_colors(y, A_FWWHITE, A_BGBLACK);
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

static void bird_scene_tick(const HiresBitmap *screen)
{
    section_background_tick(screen);
    section_clouds_tick(screen);
    section_bird_tick(screen);
    // no natural end -- never calls section_mark_finished(); runs until
    // skipped or timed out
}

// idi8b splash: min_ticks keeps an impatient keypress from insta-skipping
// before the logo has had a moment to render; max_ticks (~37s) is purely
// a safety backstop -- the section almost always finishes on its own
// (its own tick() calls section_mark_finished() once the fade-out
// completes) well before this would ever fire. Now a normal HIRES-mode
// section like any other (it used to run outside the sections[] table,
// before hires_on() -- see git history -- back when it was TEXT-mode
// content; moving it to HIRES mode removed that whole special case).
//
// EVERY *_MIN_TICKS constant in this file was trimmed from an original
// 20 (~1.2s at MAIN_FRAME_PACING_TICKS=3's ~60ms/iteration) down to 6
// (~0.36s), per user feedback that keypresses felt "very sluggish" to
// register -- 20 ticks was a genuinely long, deliberate dead zone before
// ANY keypress was even checked (see run_section()'s own
// `elapsed >= section->min_ticks` gate below), on EVERY section, which
// adds up across a demo with this many sections. 6 ticks still leaves
// enough of a grace window that a keypress can't insta-skip a section
// before anything has rendered, without the long, very-noticeable stall
// the old value produced.
#define SPLASH_MIN_TICKS 6u
#define SPLASH_MAX_TICKS 500u

// HIRES Oric logo + circling raster bars: circles indefinitely (its own
// tick() never calls section_mark_finished(), see section_logo.c), so
// min_ticks/max_ticks are the only thing pacing it -- min_ticks keeps an
// impatient keypress from insta-skipping before the bar has completed even
// one pass, max_ticks (~22s) gives a reasonable amount of time to watch
// the circling effect before moving on regardless.
#define LOGO_MIN_TICKS 6u
#define LOGO_MAX_TICKS 300u

// Bird scene: now that section #4 exists to advance into, gets real
// pacing instead of SECTION_FOREVER. Trimmed from an original ~30s
// (400 ticks) per user feedback that the overall demo ran too long.
#define BIRD_MIN_TICKS  6u
#define BIRD_MAX_TICKS 250u

// HIRES shapes showcase: its own tick() never calls section_mark_finished()
// (the 4 shapes finish building after ~60 ticks, then it just holds the
// completed picture) -- max_ticks leaves a good stretch of hold time after
// that before moving on regardless. Trimmed slightly per user feedback
// that the overall demo ran too long.
#define HIRES_SHOWCASE_MIN_TICKS  6u
#define HIRES_SHOWCASE_MAX_TICKS 120u

// Polygon workout: circles indefinitely (its own tick() never calls
// section_mark_finished(), see section_polygon_workout.c), so
// min_ticks/max_ticks are the only thing pacing it. Cut roughly in half
// from an original ~22s (300 ticks) per explicit user feedback ("shorten
// the rotating star a bit") -- a bigger trim than the other showcase
// sections below.
#define POLYGON_WORKOUT_MIN_TICKS  6u
#define POLYGON_WORKOUT_MAX_TICKS 150u

// 3D function surface: builds up over ~(9+9+18)=36 ticks (prepare/project/
// draw phases), then rotates for the remainder -- no natural end, so
// min_ticks/max_ticks pace the whole section. Trimmed per user feedback
// that the overall demo ran too long.
#define FUNC3D_MIN_TICKS  6u
#define FUNC3D_MAX_TICKS 200u

// Sprite showcase: a satellite drifts indefinitely across the starfield
// (its own tick() never calls section_mark_finished()), so min_ticks/
// max_ticks pace it. Trimmed per user feedback that the overall demo ran
// too long.
#define SPRITE_SHOWCASE_MIN_TICKS  6u
#define SPRITE_SHOWCASE_MAX_TICKS 200u

// Scroll showcase: loops its own tagline scroll indefinitely (its own
// tick() never calls section_mark_finished()). Trimmed per user feedback
// that the overall demo ran too long.
#define SCROLL_SHOWCASE_MIN_TICKS  6u
#define SCROLL_SHOWCASE_MAX_TICKS 200u

// Wave showcase: waves the magazine-photo picture indefinitely (its own
// tick() never calls section_mark_finished()). Trimmed per user feedback
// that the overall demo ran too long.
#define WAVE_SHOWCASE_MIN_TICKS  6u
#define WAVE_SHOWCASE_MAX_TICKS 200u

// Dissolve showcase: has a real natural end (its own tick() calls
// section_mark_finished() once the reveal completes, see
// section_dissolve_showcase.c) -- max_ticks is purely a safety backstop,
// generous enough that it should never actually fire. min_ticks is
// deliberately tiny: this section is a brief transition, not content to
// linger on, so an impatient keypress skipping it early is fine.
#define DISSOLVE_SHOWCASE_MIN_TICKS  5u
#define DISSOLVE_SHOWCASE_MAX_TICKS 300u

// Macaw showcase: loops its own caption scroll indefinitely (its own
// tick() never calls section_mark_finished()). Trimmed per user feedback
// that the overall demo ran too long.
#define MACAW_SHOWCASE_MIN_TICKS  6u
#define MACAW_SHOWCASE_MAX_TICKS 200u

// Raster IRQ showcase: has a real natural end (4 full top-to-bottom bar
// passes, see section_rasterirq_showcase.c's own TOTAL_PASSES) -- max_ticks
// is a generous safety backstop (4 passes * HIRES_ROWS firings / 3 raster
// ticks per main-loop iteration is ~267 iterations), should rarely fire.
#define RASTERIRQ_SHOWCASE_MIN_TICKS  6u
#define RASTERIRQ_SHOWCASE_MAX_TICKS 350u

// Credits: has a real natural end (every MSG_CREDIT_* line has scrolled
// fully off screen, see section_credits.c's own credit_lines[]/
// NUM_CREDIT_LINES) -- max_ticks is a generous safety backstop, sized
// well above the natural completion estimate (~1050 main-loop iterations
// for 12 lines at this scroller speed), should rarely fire. This is the
// LAST section -- main()'s own outer for(;;) loop below already cycles
// back to the idi8b splash once this finishes, no special "press key to
// exit" handling needed.
#define CREDITS_MIN_TICKS   6u
#define CREDITS_MAX_TICKS 1200u

// The demo's own running order -- currently the idi8b splash, the Oric
// logo/raster-bar intro, the bird scene, and the HIRES shapes showcase;
// later phases insert the remaining showcase sections/credits after (see
// this project's own planning notes for the full list).
static const DemoSection sections[] = {
    { section_splash_init, section_splash_tick, SPLASH_MIN_TICKS, SPLASH_MAX_TICKS },
    { section_logo_init, section_logo_tick, LOGO_MIN_TICKS, LOGO_MAX_TICKS },
    { bird_scene_init, bird_scene_tick, BIRD_MIN_TICKS, BIRD_MAX_TICKS },
    { section_hires_showcase_init, section_hires_showcase_tick, HIRES_SHOWCASE_MIN_TICKS, HIRES_SHOWCASE_MAX_TICKS },
    { section_polygon_workout_init, section_polygon_workout_tick, POLYGON_WORKOUT_MIN_TICKS, POLYGON_WORKOUT_MAX_TICKS },
    { section_func3d_init, section_func3d_tick, FUNC3D_MIN_TICKS, FUNC3D_MAX_TICKS },
    { section_sprite_showcase_init, section_sprite_showcase_tick, SPRITE_SHOWCASE_MIN_TICKS, SPRITE_SHOWCASE_MAX_TICKS },
    { section_scroll_showcase_init, section_scroll_showcase_tick, SCROLL_SHOWCASE_MIN_TICKS, SCROLL_SHOWCASE_MAX_TICKS },
    { section_wave_showcase_init, section_wave_showcase_tick, WAVE_SHOWCASE_MIN_TICKS, WAVE_SHOWCASE_MAX_TICKS },
    { section_dissolve_showcase_init, section_dissolve_showcase_tick, DISSOLVE_SHOWCASE_MIN_TICKS, DISSOLVE_SHOWCASE_MAX_TICKS },
    { section_macaw_showcase_init, section_macaw_showcase_tick, MACAW_SHOWCASE_MIN_TICKS, MACAW_SHOWCASE_MAX_TICKS },
    { section_rasterirq_showcase_init, section_rasterirq_showcase_tick, RASTERIRQ_SHOWCASE_MIN_TICKS, RASTERIRQ_SHOWCASE_MAX_TICKS },
    { section_credits_init, section_credits_tick, CREDITS_MIN_TICKS, CREDITS_MAX_TICKS },
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
    current_track = 1;
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
        {
            run_section(&sections[i], &screen);
            transition_clear(&screen);
        }
    }

    return 0;
}
