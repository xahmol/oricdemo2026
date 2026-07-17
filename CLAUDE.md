# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`oricdemo2026` — a finished, 12-section Oric Atmos demoscene production (see
`README.md` for the full section list and `docs/architecture.md` for the technical
writeup). Target: Oric Atmos, bare-metal (no ROM calls), 6502A @ 1 MHz.

## Toolchain

Cross-compiled with **Oscar64**, retargeted from its native C64 output to the Oric via
a custom runtime (`include/oric_crt.c`, passed as `-rt=`). This retargeting and the
library set below (`include/`) were carried over from two sibling projects that use
the same build chain: `OricScreenEditorLOCI` and `locifilemanager-v2` (both at
`~/git/`).

**Four runtimes, four programs.** `include/oric_crt.c` (default),
`include/oric_crt_hires.c` (HIRES mode), `include/oric_crt_floppy.c`
(floppy-disk target, TEXT-mode), and `include/oric_crt_floppy_hires.c`
(floppy-disk target, HIRES mode) are separate, incompatible region layouts —
see [docs/hires.md](docs/hires.md#memory-layout--the-oric_crt_hiresc-runtime)
for why HIRES bitmap graphics need `$9800-$BFDF` (which the default/floppy
runtimes use for code/data/stack), and [docs/floppy.md](docs/floppy.md) for
the floppy target's own layout and boot mechanics.
`oric_crt_floppy_hires.c` merges `oric_crt_hires.c`'s memory layout with
`oric_crt_floppy.c`'s boot-handoff entry mechanics — needed because a
floppy-target program that also wants HIRES graphics needs both at once.
`src/main.c` (the real demo, HIRES) builds with `oric_crt_hires.c` for the
tape target and `oric_crt_floppy_hires.c` for the floppy target (the SAME
source file, `#ifdef STORAGE_FLOPPY` picks the one difference: `arkos_load()`'s
signature — see that file); `src/buildtest.c` builds with the default
runtime (TEXT-mode build-chain/LOCI/Arkos regression coverage, not demo
content); `src/hires_test.c` builds with `oric_crt_hires.c` too (a separate,
library-only test fixture, not demo content either); `src/floppy_test.c`
builds with `oric_crt_floppy.c` (the floppy target's own regression
fixture, analogous to `buildtest.c`). Never mix runtimes for the same
program.

**Two distribution targets, both the real demo.** The LOCI target
(`all`/`usb`, below) and the floppy-disk target (`disk`/`run-disk`)
are BOTH the real demo (`src/main.c` + `src/section_*.c`) — no "simple tap
only" version. **The LOCI target genuinely depends on LOCI** — every
picture (`include/picture.h`) and both music tracks (`include/arkos.h`)
load from disk at runtime via `include/loci.c`'s `file_load()`, not just
music (an EARLIER state of this project only loaded music this way; that
is no longer true — don't trust old commit messages/comments implying
otherwise). Concretely: **Oricutron has no LOCI emulation at all** — this
is exactly why there is deliberately NO plain `make run` target for the
LOCI target (removed on purpose, not an oversight): it could only ever
load and run `build/oricdemo.tap`'s code while every picture/music load
silently failed (this project's own established graceful-failure
convention, not a crash), leaving a mostly-blank, silent demo. **Phosphoric
DOES emulate LOCI** (`--loci-flash <dir>`, mounting a real directory as
the LOCI's own flash storage) — `make run-phos` passes `--loci-flash
assets`, so it's the one Oricutron/Phosphoric option that actually
shows/plays the full LOCI-target demo (plus real AY audio, same as
Oricutron would give). The floppy-disk target is a bootable Microdisc
`.dsk` image needing no LOCI device and no DOS/SEDORIC resident at all —
every asset is baked into the disk image itself — see
[docs/floppy.md](docs/floppy.md); `make run-disk` in Oricutron shows the
full demo with no caveats, AND gives the same Oricutron debugger/monitor
access (F2, breakpoints) a LOCI-target Oricutron run would have had — the
reason removing plain `run` loses nothing real. Both targets are built
and tested completely
independently, and both source the SAME `src/main.c`/`src/section_*.c`
content. See `README.md`'s own Installation section for the full LOCI
distribution/install flow (10 files that must all sit in one folder).

Build chain (see `Makefile`):
```
make            # compile -> build/oricdemo.bin (Oscar64, HIRES runtime) -> build/oricdemo.tap
make run-phos   # launch the real demo (build/oricdemo.tap) visually in Phosphoric, WITH
                # LOCI emulation (--loci-flash assets) -- the full experience, real AY audio too;
                # needs PHOSDIR in .env, oric1-emu built with SDL2=1
make run-phos-buildtest # launch src/buildtest.c (build/buildtest.tap) visually in Phosphoric --
                # the build-chain/LOCI/Arkos regression test, not the real demo
make test       # Phosphoric boot smoke test (src/buildtest.c) + oric_pictconv.py/oric_voiceconv.py unit tests
make test-hires # opt-in: HIRES library Phosphoric smoke test (separate .tap, oric_crt_hires.c)
make test-pictconv # oric_pictconv.py unit tests alone (pure Python, no emulator)
make test-voiceconv # oric_voiceconv.py unit tests alone (pure Python, no emulator; also
                # enforces VOICE_SAMPLE_MAX_SIZE's safe ceiling for both voice clips, see docs/voice.md)
make test-capture CYCLES=N TYPEKEYS='...'   # calibration helper, dumps RAM+screenshot, no assertions
make usb        # copy build/oricdemo.tap + all 9 assets/*.aky|*.bin (LOCI target) + build/oricdemo_floppy.dsk to USBPATH (.env)
make docs       # README.md -> README.pdf (needs pandoc)
make zip        # release ZIP: build/oricdemo.tap + all 9 assets/*.aky|*.bin laid out under
                # idi8b/oricdemo2026/ (matching .env.example's own USBPATH convention),
                # plus build/oricdemo_floppy.dsk + README.pdf at the ZIP's top level
make disk       # floppy-disk target, REAL demo -> build/oricdemo_floppy.dsk (see docs/floppy.md)
make run-disk   # launch build/oricdemo_floppy.dsk (real demo) in Oricutron with --disk-rom microdisc.rom --
                # fully self-contained, no LOCI caveat, shows the complete demo -- this is also
                # the only Oricutron target now: there is deliberately NO plain 'make run' for the
                # LOCI target (Oricutron has no LOCI emulation at all, so it could only ever show a
                # silently-degraded demo; run-disk already gives the same Oricutron debugger access
                # -- F2, breakpoints -- with a fully working demo instead, so nothing is lost)
make test-disk  # opt-in: src/floppy_test.c's OWN regression disk (build/floppytest.dsk, separate
                # from 'disk' above -- see docs/floppy.md), Phosphoric smoke test (needs DISKROM)
make clean
```
`OSCAR64_HOME` defaults to `~/oscar64`, `ORICUTRON_HOME` to `~/oricutron` if unset.
`USBPATH` and `PHOSDIR` are read from `.env` (gitignored; copy `.env.example` to
`.env` and fill in values — see that file for WSL2 USB-mount notes). Python tooling
(`tools/oric_pictconv.py`, `tools/oric_ttfconv.py`) needs Pillow:
`pip install -r tools/requirements.txt`.

## Source layout

- `src/main.c` — the real demo's entry point, HIRES mode, built for BOTH
  distribution targets (`oric_crt_hires.c` for tape, `oric_crt_floppy_hires.c`
  for floppy — same source, `#ifdef STORAGE_FLOPPY` only changes which
  `arkos_load()`/`picture_load()` overload is used). A fixed `sections[]`
  table (11 entries, one per `src/section_*.c` pair) plus a generic
  `run_section()` runner: each section's own `init()` runs once, `tick()`
  every main-loop iteration, advancing on a natural end
  (`section_mark_finished()`), a keypress past `min_ticks`, or `max_ticks`
  regardless. The outer loop cycles the whole table forever. See
  `docs/architecture.md` for the full sequencer design and a one-line
  technique summary per section — not duplicated here.
- `src/section_background.c`/`.h`, `src/section_clouds.c`/`.h`,
  `src/section_bird.c`/`.h` — the opening bird scene (section #3): a
  static sky+creek background (plain PAPER colour bands — see that file's
  own header comment for why, and the Phosphoric rendering bug that
  constrains it), a parallax cloud layer (its own
  `clouds_scroll_left()`, deliberately NOT `hires.h`'s general
  `hb_scroll_left_fast` — see that function's own column-bytes 0-1
  caveat), and an animated bird (byte-aligned XOR sprite,
  `assets/bird.h`, 7-frame walk cycle + a fixed-point sine-wave vertical
  path) — a nod to the animated bird in the original "Welcome to Oric
  Atmos" demo (oric.org/software/welcome_to_oric_atmos-593.html).
  Exposed as `bird_scene_init()`/`bird_scene_tick()` in `main.c` (all
  three sub-modules wrapped as one `sections[]` entry).
- `src/section_splash.c`/`.h`, `src/section_logo.c`/`.h`,
  `src/section_hires_showcase.c`/`.h`, `src/section_polygon_workout.c`/`.h`,
  `src/section_func3d.c`/`.h`, `src/section_sprite_showcase.c`/`.h`,
  `src/section_scroll_showcase.c`/`.h`, `src/section_wave_showcase.c`/`.h`,
  `src/section_macaw_showcase.c`/`.h`, `src/section_rasterirq_showcase.c`/`.h`,
  `src/section_credits.c`/`.h` — the remaining 8 sections (splash, logo,
  and every showcase after the bird scene) — see `docs/architecture.md`'s
  per-section technique table, and each file's own header comment for
  full design rationale (several document real, hard-won Oscar64
  `-O2` miscompilation workarounds — don't restructure that code without
  reading those comments first).
- `src/buildtest.c` — TEXT-mode build-chain regression test (default
  `oric_crt.c` runtime; NOT demo content, not built by `all`/`usb`/`zip`).
  Inits the TEXT-mode libraries and reports LOCI/IJK detection status on
  screen, plus an Arkos decode-correctness check (loads
  `tests/fixtures/arkos_test.aky`, a tiny synthetic module, and asserts AY
  registers after 1 tick — exercising the INITIAL RegisterBlock decode path
  — and again after 4 ticks — exercising 3 more NON-INITIAL frames — see
  `docs/arkos.md`'s Verification section). Exercised by `make
  test`/`make run-phos-buildtest`. This is what used to live in `src/main.c`
  before that became the real demo above.
- `src/hires_test.c` — HIRES-mode entry point (`oric_crt_hires.c` runtime). Growing
  test fixture for `include/hires.c`/`ttf.c`, exercised by `make test-hires` —
  not demo content, see `tests/scripts/test_hires.sh` for what's asserted.
- `src/floppy_test.c` — the floppy target's OWN build-chain regression test
  (default `oric_crt_floppy.c` runtime, TEXT-mode; entered via
  `tools/floppy/loader.c`'s boot handoff, not tape auto-run) — analogous to
  `src/buildtest.c` on the tape/LOCI side. NOT demo content, not built by
  `disk`/`run-disk`. Exercises `include/floppy.c`/the resident loader/
  `arkos.c`'s `STORAGE_FLOPPY` backend, exercised by `make test-disk` (its
  own separate disk image, `build/floppytest.dsk`, distinct from the real
  demo's `build/oricdemo_floppy.dsk`) — see `docs/floppy.md` and
  `tests/scripts/test_disk.sh` for what's asserted.
- `src/strings.h` / `src/strings_en.h` — localisation gateway (`LANG=FR` ->
  `-dLANG_FR` selects `strings_fr.h`, not yet created). Only holds the two
  `MSG_*` strings `include/loci.c` needs; add app strings here as the demo grows.
- `include/` — the reusable Oric/Oscar64 library (see `docs/README.md` for the
  full API reference, one file per library):
  - `oric_crt.c` + `crt_math.c` — default Oscar64 runtime; **required**, not
    optional, for any TEXT-mode Oscar64 build targeting the Oric (see the
    memory-layout comment at the top of `oric_crt.c`).
  - `oric_crt_hires.c` — alternate runtime for HIRES-mode programs (shrunk
    `main`/`stack` regions, see `docs/hires.md`). Mutually exclusive with
    `oric_crt.c` for a given build — never both.
  - `oric_crt_floppy.c` — alternate runtime for the floppy-disk target,
    entered via `tools/floppy/loader.c`'s boot handoff at its `startup`
    region (`$0500`), not tape auto-run. Same `$0580-$B1FF` main-region
    budget as `oric_crt.c`. Mutually exclusive with both other runtimes.
    See `docs/floppy.md`.
  - `oric.h` — hardware registers/constants (VIA, AY-3-8912, TEXT screen RAM,
    HIRES bitmap, char-set RAM, attribute codes, TEXT/HIRES mode-switch attrs).
  - `keyboard.c/h` — polled matrix keyboard scanner (no ROM/IRQ).
  - `charwin.c/h` — character-window drawing/cursor/viewport library (TEXT mode).
  - `charset.c/h` — char-set RAM bank manipulation (TEXT mode).
  - `ijk.c/h` — Raxiss IJK joystick interface.
  - `loci.c/h` — LOCI storage-device driver (detection, file I/O, overlay RAM).
    Degrades gracefully when no LOCI device is attached (`loci_present()` returns
    false; don't gate the whole program on it unless the demo actually needs
    LOCI-backed storage).
  - `floppy.c/h` — LOCI-independent file loading for the floppy-disk target
    (`floppy_load(file_index, dst, max_size)`), talking to
    `tools/floppy/loader.c`'s resident loader via a fixed API trampoline at
    `$FFEF-$FFF9`. Files are addressed by a compile-time integer index (a
    fixed table baked into the disk image at build time), not a runtime
    path string — a real, intentional difference from `loci.c`, not an
    oversight. See `docs/floppy.md`.
  - `hires.c/h` — HIRES-mode bitmap graphics library (pixel/line/scroll/shape/
    pattern-fill/flood-fill/bitblit/text primitives, mode switching, ink/paper
    attributes, AIC). Requires the `oric_crt_hires.c` runtime. See `docs/hires.md`.
  - `ttf.c/h` — proportional bitmap-font rendering on top of `hires.c`, fed by
    `tools/oric_ttfconv.py`. See `docs/ttf.md`.
  - `vector3d.c/h` — 3D vector/matrix math, copied verbatim from Oscar64's own
    `include/gfx/`. Used by `src/section_func3d.c`'s rotating wireframe
    height-field mesh (perspective projection + rotation transforms). See
    `docs/vector3d.md`.
  - `fixedmath.c/h` — 256-entry fixed-point sine/cosine table (fast plasma/
    sinus-scroll effects). Generic, not HIRES-specific. See `docs/fixedmath.md`.
  - `sprite.c/h` — "save-under" sprite system on top of `hires.c`'s `hb_bitblit`.
    See `docs/sprite.md`.
  - `dissolve.c/h` — fade/dissolve transitions (strided attribute fade + LFSR
    pixel dissolve) for HIRES mode. Not used by any real demo content —
    only `src/hires_test.c`'s own test fixture exercises it. A real demo
    section built around this exact technique
    (`src/section_dissolve_showcase.c`) was tried, redesigned twice, and
    ultimately removed (a genuine two-picture crossfade wasn't safely
    achievable within this project's memory budget — see
    `docs/dissolve.md`'s own note). See `docs/dissolve.md`.
  - `rasterirq.c/h` — raster IRQ / mid-frame colour-split effects via a
    self-contained VIA Timer 1 handler. The only module that enables
    interrupts (`hrirq_start()`) — everything else in this project runs with
    interrupts permanently disabled. See `docs/rasterirq.md` **before** using
    it, especially the `__interrupt`-callback and VIA-Port-A-hazard notes.
  - `ay.c/h` — AY-3-8912 register-write helper (correct VIA/PCR protocol;
    an earlier version of `oric.h`'s own comment described the wrong one).
    See `docs/ay.md`.
  - `arkos.c/h` — Arkos Tracker (`.aky`) music player, ticking via
    `rasterirq.h` at 50Hz. Replaces an earlier PT3 (Vortex Tracker) player
    (archived on the `pt3` branch after several rounds of decode-bug fixes
    still didn't produce satisfying music, and its runtime overhead was
    judged too high). Unlike PT3, `.aky` files bake ABSOLUTE 16-bit
    pointers into the Linker/Track tables at Arkos Tracker's own export
    time — no relocation is done by this player, so every module MUST be
    exported to (and is loaded at) a single fixed address, `$C000`, this
    project's own overlay-RAM buffer (see `docs/arkos.md` for the full
    memory-layout rationale, including why the tape/LOCI target needs
    `loci.c`'s `enable_overlay_ram()` first and the floppy target doesn't).
    Loads tunes via `loci.c` by default, or `floppy.c` under
    `-dSTORAGE_FLOPPY` — `arkos_load()`'s signature differs by target
    (runtime path string vs. compile-time file index), a real, intentional
    difference, not a bug (see `docs/floppy.md`). See `docs/arkos.md`.
  - `picture.c/h` — runtime loader for pre-rendered HIRES pictures
    (`tools/oric_pictconv.py`'s `--format bin` output), same reasoning and
    same dual-signature `#ifdef STORAGE_FLOPPY` convention as `arkos.c/h`
    (keeps ~8000 bytes/picture out of the main code/data/BSS budget).
    Every real picture in the demo loads through this, not a compiled-in
    array. See `docs/picture.md`.
  - `voice.c/h` — AY-3-8912 "digidrums"-style voice-sample playback: two
    hardcoded clips, "Welcome to Oric Atmos" (played once from
    `src/section_logo.c`, right after the logo picture loads) and
    "Thanks for watching" (played once from `src/section_credits.c`),
    sharing one fixed
    overlay-RAM address since they're never resident/playing at the same
    time. Same dual-signature loading convention as `arkos.c/h`/
    `picture.c/h`; the actual playback rewrites `AY_REG_VOL_A` from a
    pre-quantized sample buffer, each clip paced at its own per-call
    rate (VIA Timer 1 reprogrammed to a compile-time-constant period,
    not one shared fixed rate), with music paused and interrupts off
    for the duration. See `docs/voice.md`.
- `tools/mktap.py` — wraps an Oscar64 raw `.bin` in an Oric `.tap` tape header.
- `tools/oric_pictconv.py` — JPG/PNG -> HIRES bitmap converter (mono/colored/aic
  modes). See `docs/pictconv.md`.
- `tools/oric_voiceconv.py` — WAV -> AY-3-8912 4-bit voice-sample converter
  (stdlib only). See `docs/voice.md`.
- `tools/oric_ttfconv.py` — TTF -> proportional HIRES bitmap font converter, for
  `include/ttf.h`. See `docs/ttf.md`.
- `tools/oric_floppybuilder.py` — Python port of OSDK's FloppyBuilder tool,
  assembling the floppy-disk target's bootable `.dsk` image from a script
  (`tools/floppy/disk_script.txt`). See `docs/floppy.md`.
- `tools/floppy/` — the floppy-disk target's boot sector
  (`bootsector_microdisc.c`), resident loader (`loader.c`), and supporting
  build artifacts (`extract_bootsector.py`, fixed sector-content fixtures).
  Standalone Oscar64 programs, not `include/`-library files — never
  `#include`d by application code. See `docs/floppy.md`.
- `tools/requirements.txt` — Pillow, needed by both Python converters above.
- `tests/` — Phosphoric-based headless testing:
  - `tests/scripts/oric_screen.py` — decodes the 40x28 TEXT screen, or hex-dumps
    arbitrary bytes (`--bytes ADDR:LEN`), out of a `--dump-ram-at` RAM dump.
  - `tests/scripts/test_boot.sh` — the `make test` boot smoke test (TEXT mode,
    `src/main.c`); asserts the status lines render.
  - `tests/scripts/test_hires.sh` — the `make test-hires` smoke test (HIRES mode,
    `src/hires_test.c`); byte-exact assertions via `oric_screen.py --bytes`.
  - `tests/scripts/test_disk.sh` — the `make test-disk` smoke test (floppy
    target, `src/floppy_test.c`); boots `build/oricdemo_floppy.dsk` under
    Phosphoric's Microdisc emulation (`--disk-rom`, no LOCI/tape at all) and
    asserts the status lines, a `floppy_load()` payload check, and an
    `arkos_load(file_index)` AY-register assertion. See `docs/floppy.md`.
  - `tests/scripts/test_pictconv.py` — `oric_pictconv.py` unit tests (`make
    test-pictconv`), pure Python, no emulator.
  - `tests/scripts/test_voiceconv.py` — `oric_voiceconv.py` unit tests (`make
    test-voiceconv`), pure Python, no emulator; also asserts BOTH voice
    clips' real sizes stay within `VOICE_SAMPLE_MAX_SIZE`'s shared safe
    ceiling. See `docs/voice.md`.
  - `tests/scripts/test_floppybuilder.py` — `tools/oric_floppybuilder.py`
    unit tests, pure Python, no emulator. See `docs/floppy.md`.
  - `tests/fixtures/` — files copied into `tests/sandbox/` before each Phosphoric
    test run, plus checked-in test images/expected `.bin`s for
    `test_pictconv.py`, `tests/fixtures/ttf_test_font.h` (a pre-generated
    font header so `test_hires.sh` doesn't depend on a system font), and
    `tests/fixtures/arkos_test.aky` (a tiny hand-built synthetic module, not
    a real tune, for `arkos.c`'s decode-correctness tests — see
    `docs/arkos.md`). Phosphoric needs `--loci-flash tests/sandbox` (not
    just bare `--loci`) for `arkos_load()`'s `file_load()` to actually
    find it.
  - `tests/sandbox/`, `tests/out/` — gitignored scratch, regenerated per run.
- `assets/` — real demo assets consumed by `src/section_*.c` files (as opposed
  to `tests/fixtures/`, which only test scaffolding uses): 2 Arkos `.aky`
  music modules, 6 `oric_pictconv.py`-converted `.bin` pictures (loaded at
  runtime via `include/picture.h`, see above — none compiled in),
  `assets/voice_welcome.bin` and `assets/voice_thanks.bin`
  (`tools/oric_voiceconv.py`-converted AY digidrums voice samples, loaded
  via `include/voice.h` — see above), and `assets/bird.h` (7-frame
  walk-cycle sprite data for
  `src/section_bird.c`, adapted from mihai-dragan's `oric_BAS` project,
  MIT License, github.com/xahmol/sprites — see that file's header comment
  for the full attribution note). Every asset's exact source/license is
  credited in a header comment at its own point of use in `src/`, and
  summarized in `README.md`'s own Credits/Installation sections.
- `oscar64manual.md` — Oscar64 compiler reference; `docs/` — per-library API
  reference (`docs/README.md` is the index); consult before re-deriving
  Oscar64 compiler behavior or library APIs from scratch.

## Notes

- `src/buildtest.c`/`src/hires_test.c`/`src/floppy_test.c` are build-chain
  smoke tests proving each runtime end-to-end (`make test`/`make test-hires`/
  `make test-disk`), not demo content. `src/main.c` (+ `src/section_*.c`) IS
  the real demo content, built for both distribution targets. The demo is
  feature-complete (12 sections, `docs/architecture.md`'s own table) — a
  request to add another section is a new scope decision, not a
  continuation of an existing plan; nothing is "still to come."
- The TEXT-mode `include/` library files (`charwin`, `keyboard`, `charset`,
  `ijk`, `loci`) are shared with `OricScreenEditorLOCI` and `locifilemanager-v2`
  but have already diverged between those two (they are copies, not a shared
  package) — don't assume changes here propagate, or that changes there apply
  here. `hires.c/h`, `ttf.c/h`, `oric_crt_hires.c`, `oric_crt_floppy.c`,
  `floppy.c/h`, `tools/floppy/`, `tools/oric_floppybuilder.py`, and the other
  Python tools are original to this project, not carried over from those
  siblings.
- HIRES-mode programs get **less usable RAM** than TEXT-mode ones (~36.1 KB vs.
  ~42.4 KB code/data/bss) — see `docs/hires.md`'s memory-layout table before
  assuming the default runtime's budget applies.
