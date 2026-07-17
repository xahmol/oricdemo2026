# Architecture

Technical reference for `oricdemo2026`'s final, shipped design: memory
maps, the section sequencer, per-section techniques, and the shared
subsystems every section builds on. For the `include/` library API
itself (function signatures, parameters), see [docs/README.md](README.md)
and its per-library pages — this document is about how the pieces fit
together, not a function reference.

## Two distribution targets, one source

`src/main.c` + `src/section_*.c` is the entire demo, compiled twice:

| | Tape/LOCI | Floppy disk |
|---|---|---|
| Output | `build/oricdemo.tap` | `build/oricdemo_floppy.dsk` |
| Runtime | `include/oric_crt_hires.c` | `include/oric_crt_floppy_hires.c` |
| Entry | BASIC tape auto-run | `tools/floppy/loader.c`'s boot handoff |
| Storage | LOCI (`include/loci.c`) | Resident loader (`include/floppy.c`) |
| Asset addressing | Path string (`"macaw.bin"`) | Compile-time file index (`7`) |
| Music/picture buffer | `$C000`, LOCI overlay RAM | `$C000`, plain RAM |

Both targets build from the *same* `#ifdef STORAGE_FLOPPY` source files —
see `arkos.h`/`picture.h`'s own header comments for the dual-signature
convention this relies on. Two more runtimes exist
(`include/oric_crt.c`, `include/oric_crt_floppy.c`) but are **not** used
by the real demo — they back `src/buildtest.c`/`src/floppy_test.c`, the
build-chain regression fixtures for each target (`make test`/`make
test-disk`), and `src/hires_test.c` (`make test-hires`, HIRES library
fixture, tape-target runtime).

## Memory maps

Both real-demo runtimes reserve the same ~36.1 KB code/data/BSS budget
(smaller than the ~42.4 KB the two non-HIRES runtimes get — HIRES bitmap
graphics need the extra address space below):

**Tape/LOCI target (`oric_crt_hires.c`):**
```
$0000-$00FF  Zero page (Oscar64 internal registers)
$0100-$01FF  6502 hardware stack
$0200-$04FF  Oric ROM system variables
$0500-$057F  Startup region (tape entry point)
$0580-$95FF  Program code, data, BSS (~36.1 KB)
$9600-$97FF  6502 software stack (512 bytes)
$9800-$9BFF  HIRES standard charset bank (HIRES_CHARSET_STD)
$9C00-$9FFF  HIRES alternate charset bank (HIRES_CHARSET_ALT)
$A000-$BF3F  HIRES bitmap (HIRESVRAM, 8000 bytes)
$BF40-$BF67  Unused (42 bytes)
$BF68-$BFDF  Built-in 3-line TEXT footer (HIRES_FOOTER)
$C000-$F9FF  Overlay RAM when enabled (14848 bytes) -- Arkos music module
             at its base $C000; real Atmos ROM otherwise
```

**Floppy target (`oric_crt_floppy_hires.c`)** — identical `$0580-$BFDF`
layout, entered via the loader's boot handoff instead of tape auto-run,
plus:
```
$C000-$F9FF  Plain RAM (no ROM/overlay concept on this target) -- Arkos
             music module at $C000, same address as the tape target
$FA00-$FFFF  OFF LIMITS -- tools/floppy/loader.c's resident code + its
             fixed API/vector block ($FFEF-$FFFF), live for the whole
             demo's runtime (every floppy_load() call is a `jsr $FFF7`
             into this range)
```

The `$C000-$F9FF` ceiling (14848 bytes, not the full 16 KB/64 KB) is
shared by both targets for one reason on the tape target (real ROM
occupies the rest) and a different reason on the floppy target (the
loader's own reserved range) — see `docs/arkos.md` for the full
rationale. **This region has a real, non-obvious constraint**: it holds
whichever Arkos music track is currently loaded, and the two shipped
tracks leave very different amounts of free slack (`steppingout.aky`
5933 bytes → 8915 free; `boulesetbits.aky` 7117 bytes → 7731 free) — a
feature that assumed ~8000 bytes of guaranteed slack here (an off-screen
picture staging buffer, attempted for a real crossfade transition) was
abandoned specifically because the numbers don't reliably work for both
tracks. Check real current asset sizes before building anything else
that shares this window.

## The section sequencer (`src/main.c`)

```c
typedef struct {
    void (*init)(const HiresBitmap *screen);
    void (*tick)(const HiresBitmap *screen);
    uint16_t min_ticks;
    uint16_t max_ticks;
} DemoSection;
```

A fixed `sections[]` table (12 entries) plus a generic runner:
`run_section()` calls `init` once, then `tick` every main-loop iteration,
paced by a real 50Hz raster-IRQ tick counter (`main_frame_tick`,
`MAIN_FRAME_PACING_TICKS=3` → ~60ms/iteration). A section advances when
(a) its own `tick()` calls `section_mark_finished()` (a real natural end —
see `section_common.h`'s own header comment for why this is a plain
function call, not a `bool` return, working around a real Oscar64
code-generation bug), (b) `min_ticks` has elapsed and a key is pressed, or
(c) `max_ticks` is reached regardless. `main()`'s own outer `for(;;)`
loop cycles through the whole table forever — credits (the last entry)
loops back into the splash, no special "press key to exit" needed.

Between every section, `transition_clear()` sweeps a right-to-left
blank-and-reset wipe (raw `memset`, not `hb_rect_fill()` — the latter
measured as tens of millions of wasted cycles for a solid full-height
band, see that function's own header comment) before the next section's
`init()` runs, so no section ever inherits stray pixel/attribute state
from the one before it. Right-to-left, not left-to-right: each row's
own ink/paper attribute bytes live at column-bytes 0-1, so sweeping
that direction touches them LAST, avoiding a real, previously-confirmed
bug where the outgoing scene briefly flashed to the ULA's hardware-default
white-ink/black-paper for the rest of the transition.

## Per-section techniques

| # | Section | Technique |
|---|---|---|
| 1 | `section_splash` | Per-cell dissolve reveal of a mosaic wordmark, using the real Oric ROM ALT-charset glyph data (extracted empirically, since ALT-charset RAM content isn't documented) |
| 2 | `section_logo` | A converted wordmark picture with two rotating, colour-cycling highlight bars — static per-tick attribute writes (`hires_row_colors()`), not raster-IRQ timing (Oric has no live colour register to need it — see below) |
| 3 | `section_background`/`_clouds`/`_bird` | Procedural colour-band sky/creek, a parallax cloud sub-canvas, and a byte-aligned XOR sprite flying a fixed-point sine path |
| 4 | `section_hires_showcase` | Four `hires.h` fill primitives in turn: ellipse, star (`hb_line()` outline + `hb_flood_fill()`), pattern-fill, flood-fill |
| 5 | `section_polygon_workout` | A continuously rotating/pulsing wireframe star, `hb_line()` only (see below) |
| 6 | `section_func3d` | A rotating 3D wireframe height-field mesh via `vector3d.h`'s real perspective-camera pipeline, small-batched per-tick state machine, no interrupt brackets needed |
| 7 | `section_sprite_showcase` | A drifting satellite sprite over a procedurally-generated, independently-scrolling starfield (byte-exact save/restore over busy content; two-speed parallax between the two) |
| 8 | `section_scroll_showcase` | A byte-aligned (6px/step) hardware ROM-charset scroller over a converted desk-scene picture |
| 9 | `section_wave_showcase` | A converted magazine photo, sine-distorted in place |
| 10 | `section_macaw_showcase` | A full-colour (`pictoric`-mode converted) macaw photo with a scrolling caption |
| 11 | `section_rasterirq_showcase` | Three colour bars driven by a genuine `hrirq_add()` `__interrupt` callback at 50Hz (not main-loop-paced), over three procedurally-drawn stars (filled/hollow/patterned) |
| 12 | `section_credits` | The scroller engine chained through multiple lines, over a converted sunset photo |

Numbering matches the actual `sections[]` array order in `main.c`.

## Shared subsystems

### HIRES bitmap graphics (`hires.h`)

Oric HIRES has **no per-pixel colour** — ink/paper is a *serial
attribute*: a control byte embedded in the video data stream itself
(`bit6=0`), changing colour for the rest of that raster line from that
column onward. There is no separate hardware colour register at all
(confirmed against `~/.claude/oric_atmos_reference.md`) — this is why
`section_logo.c`'s own colour bars use plain per-tick attribute writes
rather than raster-IRQ timing: writing an attribute byte ahead of time
looks *identical* to writing it via precisely-timed interrupt, since the
ULA just reads whatever's there when the beam arrives. Genuine mid-frame
IRQ timing only matters when the same screen position needs to show
multiple different values within one frame — which no effect in this
demo actually needs; `section_rasterirq_showcase.c`'s own `hrirq_add()`
callback exists for a *different* reason (a 50Hz update rate, smoother
than the main loop's own ~16.7Hz pacing), not colour-split precision.

**No general polygon/triangle fill.** `hb_polygon_fill()`/
`hb_triangle_fill()` were removed from the library entirely — a
division-heavy per-pixel point-in-polygon test that took several real
seconds to fill an ordinary shape on this 1MHz CPU, on top of a
documented, unresolved Oscar64 `-O2` miscompilation risk (see
`~/.claude/oscar64.md`). Every real caller now draws a shape's outline
via `hb_line()` and flood-fills from a known-interior seed point instead
— see `docs/hires.md`'s own section on this for the full rationale and
worked examples.

### Music (Arkos Tracker, `arkos.h`)

A `.aky` module loads once into the fixed `$C000` buffer described
above, ticking at 50Hz via a raster-IRQ callback (`arkos_tick()`,
registered once in `main()`, running for the demo's entire lifetime).
Two tracks alternate automatically whenever the current one finishes a
full playthrough (`arkos_song_finished()`, checked once per main-loop
iteration via `music_check_toggle()`). `arkos_pause()`/`arkos_resume()`
(distinct from `arkos_stop()`/`arkos_init()`) let `picture_load()`
briefly silence output around a file load without losing the current
track's position — see `docs/arkos.md`'s "Pause vs. stop" section.

### Raster IRQ (`rasterirq.h`)

A single self-contained VIA Timer 1 `__hwinterrupt` handler
(`_hrirq_handler`), never falling through to the ROM's own dispatcher.
Up to 8 callbacks can be registered (`hrirq_add()`); this demo uses 3 for
its entire runtime: `arkos_tick()` (music), `main_frame_tick_isr()`
(section pacing), and `rasterbar_isr()` (registered once, on the raster
IRQ showcase's first pass — see that section's own header comment for
why it's guarded against re-registering on every subsequent demo loop,
and why a `rb_active` flag guards it from corrupting later sections,
since `hrirq_add()` has no "remove callback" primitive). Both
`section_func3d.c`'s mesh drawing and `section_rasterirq_showcase.c`'s
bars had to be carefully paced (small per-tick batches) to avoid
starving `arkos_tick()` of its own 50Hz budget — a real, recurring class
of bug in this project (see `~/.claude/oscar64.md` and both sections'
own header comments).

### Picture loading (`picture.h`)

Every pre-rendered picture (`tools/oric_pictconv.py` output, raw
`--format bin`) loads from disk/LOCI at runtime rather than being
compiled into a C array — this keeps ~8000 bytes per image entirely out
of the tight ~36.1 KB code/data/BSS budget, the same reasoning that keeps
the Arkos module out of it. Same dual-signature convention as
`arkos_load()`. On the floppy target, assets are addressed by a
compile-time file index baked into the disk image
(`tools/floppy/disk_script_demo.txt`) — currently: 0=demo binary,
1=steppingout.aky, 2=oriclogo.bin, 3=boulesetbits.aky, 4=oricatmos.bin,
5=oricmag.bin, 6=macaw.bin, 7=sunset.bin. (The starfield picture that
used to be file index 4 is gone -- see the per-section technique table
above: `section_sprite_showcase` now draws its starfield procedurally,
no picture asset at all.)

### Text scrolling (`scroller.h`)

A byte-aligned (6px/step) scroller writing raw ROM-charset glyph bytes
directly into HIRES VRAM — no per-pixel `hb_put()` calls, and the byte
copy naturally overwrites/erases the previous frame in the same pass (no
separate erase step). Reused unchanged by `section_scroll_showcase.c`,
`section_macaw_showcase.c`, and `section_credits.c` (which just chains
multiple strings through the same one-string-at-a-time engine).

### Floppy boot (`tools/floppy/`, `docs/floppy.md`)

A from-scratch Microdisc boot sector + resident loader (Oscar64-compiled,
not hand-assembled), assembled into a bootable `.dsk` image by
`tools/oric_floppybuilder.py` (a Python port of OSDK's own FloppyBuilder
tool). No LOCI, no DOS/SEDORIC resident needed at all. See
`docs/floppy.md` for the full boot-sector/directory-sanity-sector/loader
mechanics — genuinely non-obvious, byte-traced against OSDK's own
reference implementation.

## Known Oscar64 gotchas (summary — see `~/.claude/oscar64.md` for full detail)

This project has hit the same general bug class — an Oscar64 `-O2`
whole-program register-allocator defect — multiple times, in different
concrete shapes:

- A caller's own live local variable silently corrupted by an unrelated
  callee's changing complexity/size.
- A shared library function (`hb_polygon_fill()`, `hb_line()` at specific
  call depths) silently dropping loop iterations, ONLY at some call
  sites, resistant to the usual `__noinline`/helper-extraction fixes.
- An unrelated `__interrupt` handler (Arkos's own music tick) corrupted
  by changes to completely unrelated code elsewhere in the program.

**Practical takeaway for future work on this codebase**: never trust a
single build/run as proof a fix works — this bug class is real, current
Oscar64 behavior, and requires an actual soak test (hundreds of millions
of cycles, both targets) plus visual/register verification before
trusting any change touching interrupt-adjacent code or shared drawing
primitives. When a call site hits this class and the standard fixes
don't stick, this project's own established response is to route around
the specific function entirely (as `hb_polygon_fill()`'s removal did)
rather than keep debugging the compiler.

## Testing

- `make test` — tape-target boot smoke test (`src/buildtest.c`) +
  `oric_pictconv.py`/`oric_floppybuilder.py` unit tests. Runs headless
  under Phosphoric.
- `make test-disk` — floppy-target boot smoke test (`src/floppy_test.c`),
  its own separate regression disk image.
- `make test-hires` (opt-in) — HIRES library fixture (`src/hires_test.c`),
  byte-exact VRAM assertions for most `hires.h` primitives.
- No automated test exercises the real demo's own visual content
  end-to-end (12 sections' worth of animation/timing isn't practical to
  byte-assert) — every section landed in this project was verified via
  headless Phosphoric screenshots/frame-dumps and/or RAM-dump byte
  inspection at specific cycle counts during development, not via a
  checked-in automated test.
