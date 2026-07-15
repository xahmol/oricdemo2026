// section_dissolve_showcase.c - see section_dissolve_showcase.h.
//
// Round 12 redesign, per explicit user feedback: the original version
// (a stride-strobed attribute fade then an LFSR pixel reveal, both
// reimplemented locally from include/dissolve.h -- see git history)
// never actually showed a transition BETWEEN the two real pictures either
// side of it. main.c's own main loop always runs transition_clear() (a
// left-to-right wipe to a blank baseline) between EVERY section,
// including right before this one's own init() -- so this section's
// canvas started genuinely blank, not showing the wave-showcase photo,
// and the LFSR phase only ever revealed random WHITE PIXELS on that
// blank canvas, ending in a solid white screen with no picture content
// at all. Visually correct per its own (as-built) design, but not a real
// "dissolve LINK between the two pictures" -- per the user's own words,
// "this only makes sense if it dissolves from something instead of only
// dots."
//
// Fixed by making this section genuinely self-contained rather than
// relying on whatever transition_clear() happened to leave on screen:
// it loads BOTH real pictures itself. init() loads assets/oricmag.bin
// (the wave-showcase photo) as the starting frame; each tick() then
// reveals MORE of assets/macaw.bin from the top down, by calling
// picture_load() again with a GROWING max_size -- picture_load() (and
// the file_load()/floppy_load() underneath it) only ever writes the
// first `max_size` bytes of the destination, leaving whatever was
// already there in the untouched tail (confirmed by reading picture.c
// directly) -- so each step overwrites the top N rows with real macaw
// pixel data while the wave photo's own bytes remain visible in the
// rows not yet reached. This reads as a real top-down wipe from one
// real picture into another, no random dots, and needs NO extra RAM
// (no off-screen buffer for the incoming picture -- a full 8000-byte
// buffer was never in this project's tight ~3-4KB remaining budget
// anyway) and no new loader plumbing: it's the exact same picture_load()
// every other section already uses, just called several times with an
// increasing size instead of once with the full size.
//
// NOT a true smooth crossfade (that would need a genuine per-pixel or
// per-row RANDOM reveal order blending real bytes from both images,
// which -- since file_load()/floppy_load() only support reading
// sequentially from a file's own start, not an arbitrary offset --
// would need a real off-screen staging buffer this project doesn't have
// spare RAM for). A top-down wipe is the achievable middle ground: a
// real picture-to-picture transition, at zero extra RAM cost.

#include "oric.h"
#include "hires.h"
#include "picture.h"
#include "section_dissolve_showcase.h"
#include "section_common.h"

#ifdef STORAGE_FLOPPY
#define ORICMAG_FILE 6
#define MACAW_FILE   7
#else
#define ORICMAG_FILE "oricmag.bin"
#define MACAW_FILE   "macaw.bin"
#endif

// 10 rows/step (400 bytes) -- enough steps (20) for a visibly gradual
// wipe without needing an excessive number of picture_load() calls (each
// one re-reads the file from its own start, so total I/O across the
// whole reveal grows with the NUMBER of steps, not just the final size).
#define DISSOLVE_ROWS_PER_STEP 10u
#define DISSOLVE_BYTES_PER_STEP ((uint16_t)DISSOLVE_ROWS_PER_STEP * HIRES_ROW_BYTES)
#define DISSOLVE_TOTAL_STEPS (HIRES_ROWS / DISSOLVE_ROWS_PER_STEP)

// Ticks (main-loop iterations, ~60ms each -- MAIN_FRAME_PACING_TICKS in
// main.c) to WAIT between advancing dissolve_step -- NOT more picture_load()
// calls (that would grow total I/O, see the comment above), just more real
// time between the SAME 20 steps. Without this, 20 steps at one per tick
// is only ~1.2s total -- too fast to read as a flowing wipe (reported by
// the user as "just shortly shows the [wave photo] and the [macaw], but
// not flowing into each other"). 4 ticks/step -> ~4.8s total, a real,
// perceptible top-down reveal.
#define DISSOLVE_TICKS_PER_STEP 4u

static uint8_t dissolve_step;
static uint8_t step_tick_count;

void section_dissolve_showcase_init(const HiresBitmap *screen)
{
    (void)screen;
    picture_load(ORICMAG_FILE, (void *)HIRESVRAM, 8000);
    dissolve_step = 0;
    step_tick_count = 0;
}

// void, not bool -- see section_common.h's own header comment for why.
// Calls section_mark_finished() once the wipe reaches the bottom of the
// screen -- this section has a real, natural end (unlike the wave/scroll
// sections either side of it), so main.c advances into
// section_macaw_showcase as soon as it's done rather than waiting out
// its own max_ticks.
void section_dissolve_showcase_tick(const HiresBitmap *screen)
{
    (void)screen;
    step_tick_count++;
    if (step_tick_count < DISSOLVE_TICKS_PER_STEP)
        return;
    step_tick_count = 0;

    dissolve_step++;
    picture_load(MACAW_FILE, (void *)HIRESVRAM, (uint16_t)dissolve_step * DISSOLVE_BYTES_PER_STEP);
    if (dissolve_step >= DISSOLVE_TOTAL_STEPS)
        section_mark_finished();
}
