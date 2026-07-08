# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`oricdemo2026` — an Oric computer demoscene project (per README: "Testing vibe coding
an Oric demo"). Target: Oric Atmos, bare-metal (no ROM calls), 6502A @ 1 MHz.

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
source file, `#ifdef STORAGE_FLOPPY` picks the one difference: `pt3_load()`'s
signature — see that file); `src/buildtest.c` builds with the default
runtime (TEXT-mode build-chain/LOCI/PT3 regression coverage, not demo
content); `src/hires_test.c` builds with `oric_crt_hires.c` too (a separate,
library-only test fixture, not demo content either); `src/floppy_test.c`
builds with `oric_crt_floppy.c` (the floppy target's own regression
fixture, analogous to `buildtest.c`). Never mix runtimes for the same
program.

**Two distribution targets, both now the real demo.** The tape target
(`all`/`run`/`usb`, below) and the floppy-disk target (`disk`/`run-disk`)
are BOTH the real demo (`src/main.c` + `src/section_*.c`) — no "simple tap
only" version. Tape/LOCI no longer depends on LOCI for graphics (only for
loading its music file at runtime), so it runs fine in plain Oricutron,
including real AY audio (Oricutron emulates AY audio; Phosphoric doesn't —
see `make run-phos` below). The floppy-disk target is a bootable Microdisc
`.dsk` image needing no LOCI device and no DOS/SEDORIC resident, with the
music file baked into the disk image instead — see
[docs/floppy.md](docs/floppy.md). Both targets are built and tested
completely independently, and both source the SAME `src/main.c`/
`src/section_*.c` content.

Build chain (see `Makefile`):
```
make            # compile -> build/oricdemo.bin (Oscar64, HIRES runtime) -> build/oricdemo.tap
make run        # launch the real demo (build/oricdemo.tap) in Oricutron (needs ORICUTRON_HOME)
make run-phos   # launch src/buildtest.c (build/buildtest.tap) visually in Phosphoric instead --
                # the build-chain/LOCI/PT3 regression test, not the real demo (see above)
                # (needs PHOSDIR in .env; oric1-emu must be built with SDL2=1)
make test       # Phosphoric boot smoke test (src/buildtest.c) + oric_pictconv.py unit tests
make test-hires # opt-in: HIRES library Phosphoric smoke test (separate .tap, oric_crt_hires.c)
make test-pictconv # oric_pictconv.py unit tests alone (pure Python, no emulator)
make test-capture CYCLES=N TYPEKEYS='...'   # calibration helper, dumps RAM+screenshot, no assertions
make usb        # copy build/oricdemo.tap + assets/oxygene4.pt3 (real demo + music) to USBPATH (.env)
make docs       # README.md -> README.pdf (needs pandoc)
make zip        # release ZIP (build/oricdemo.tap + assets/oxygene4.pt3 + README.pdf)
make disk       # floppy-disk target, REAL demo -> build/oricdemo_floppy.dsk (see docs/floppy.md)
make run-disk   # launch build/oricdemo_floppy.dsk (real demo) in Oricutron with --disk-rom microdisc.rom
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
  `pt3_load()` overload/music-file-reference is used). A thin sequencer:
  `hires_init()`/mode-switch/background/footer/music setup, then a master
  loop calling each section's own `_tick()` function every iteration (see
  `src/section_bird.h`/`src/section_clouds.h`) — sections own their state,
  `main.c` just drives the shared loop so multiple animated sections (bird,
  clouds) run concurrently rather than each hogging its own `for(;;)`.
- `src/section_background.c`/`.h` — draws the static sky+creek background
  once at startup (plain PAPER colour bands: cyan sky, blue creek — see
  that file's own header comment for why it's colour-only, no pixel
  texture, and the known Phosphoric rendering bug that constrains it).
- `src/section_clouds.c`/`.h` — parallax cloud layer in the sky's upper
  rows: a `HiresBitmap` sub-canvas scrolled independently of the bird via
  its own `clouds_scroll_left()` (deliberately NOT `hires.h`'s general
  `hb_scroll_left_fast` — see that function's own caveat about column-bytes
  0-1). Ticks on its own cadence (every `CLOUD_SCROLL_EVERY` main-loop
  ticks), giving a two-speed depth effect against the bird's own per-tick
  horizontal movement.
- `src/section_bird.c`/`.h` — animated bird flying both horizontally
  (byte-aligned XOR sprite via `include/sprite.h`, 7-frame walk cycle) and
  vertically (a sine wave via `include/fixedmath.h`'s `oric_sin()`), a nod
  to the animated bird in the original "Welcome to Oric Atmos" demo
  (oric.org/software/welcome_to_oric_atmos-593.html). Frame data is
  `assets/bird.h`. Exposes `section_bird_init()`/`section_bird_tick()`
  (not a `_run()` loop) — see `main.c`'s master loop above.
- `src/buildtest.c` — TEXT-mode build-chain regression test (default
  `oric_crt.c` runtime; NOT demo content, not built by `all`/`usb`/`zip`).
  Inits the TEXT-mode libraries and reports LOCI/IJK detection status on
  screen, plus two PT3 decode-correctness checks (loads
  `tests/fixtures/music.pt3` for one tick, then `music_effects.pt3` for five
  ticks exercising portamento/vibrato/envelope-glide, printing the computed
  AY register values each time — see `docs/pt3.md`'s Verification section).
  Exercised by `make test`/`make run-phos`. This is what used to live in
  `src/main.c` before that became the real demo above.
- `src/hires_test.c` — HIRES-mode entry point (`oric_crt_hires.c` runtime). Growing
  test fixture for `include/hires.c`/`ttf.c`, exercised by `make test-hires` —
  not demo content, see `tests/scripts/test_hires.sh` for what's asserted.
- `src/floppy_test.c` — the floppy target's OWN build-chain regression test
  (default `oric_crt_floppy.c` runtime, TEXT-mode; entered via
  `tools/floppy/loader.c`'s boot handoff, not tape auto-run) — analogous to
  `src/buildtest.c` on the tape/LOCI side. NOT demo content, not built by
  `disk`/`run-disk`. Exercises `include/floppy.c`/the resident loader/
  `pt3.c`'s `STORAGE_FLOPPY` backend, exercised by `make test-disk` (its
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
    `include/gfx/`. Not currently used by any demo code. See `docs/vector3d.md`.
  - `fixedmath.c/h` — 256-entry fixed-point sine/cosine table (fast plasma/
    sinus-scroll effects). Generic, not HIRES-specific. See `docs/fixedmath.md`.
  - `sprite.c/h` — "save-under" sprite system on top of `hires.c`'s `hb_bitblit`.
    See `docs/sprite.md`.
  - `dissolve.c/h` — fade/dissolve transitions (strided attribute fade + LFSR
    pixel dissolve) for HIRES mode. See `docs/dissolve.md`.
  - `rasterirq.c/h` — raster IRQ / mid-frame colour-split effects via a
    self-contained VIA Timer 1 handler. The only module that enables
    interrupts (`hrirq_start()`) — everything else in this project runs with
    interrupts permanently disabled. See `docs/rasterirq.md` **before** using
    it, especially the `__interrupt`-callback and VIA-Port-A-hazard notes.
  - `ay.c/h` — AY-3-8912 register-write helper (correct VIA/PCR protocol;
    an earlier version of `oric.h`'s own comment described the wrong one).
    See `docs/ay.md`.
  - `pt3.c/h` — PT3 (Vortex Tracker) music player, ticking via `rasterirq.h`
    at 50Hz (reprograms Timer 1's rate — see `docs/rasterirq.md`'s note on
    this). Loads tunes via `loci.c` by default, or `floppy.c` under
    `-dSTORAGE_FLOPPY` — `pt3_load()`'s signature differs by target
    (runtime path string vs. compile-time file index), a real, intentional
    difference, not a bug (see `docs/floppy.md`). Notes, ornaments, samples,
    volume, noise, envelope, tempo, and all four effects (portamento,
    glissando, vibrato, envelope-glide) are implemented — the effects use a
    standard, musically-correct design rather than a bit-exact replica of
    the reference's own bookkeeping for those four. See `docs/pt3.md`.
- `tools/mktap.py` — wraps an Oscar64 raw `.bin` in an Oric `.tap` tape header.
- `tools/oric_pictconv.py` — JPG/PNG -> HIRES bitmap converter (mono/colored/aic
  modes). See `docs/pictconv.md`.
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
    asserts the status lines, a `floppy_load()` payload check, and a
    `pt3_load(file_index)` AY-register assertion. See `docs/floppy.md`.
  - `tests/scripts/test_pictconv.py` — `oric_pictconv.py` unit tests (`make
    test-pictconv`), pure Python, no emulator.
  - `tests/scripts/test_floppybuilder.py` — `tools/oric_floppybuilder.py`
    unit tests, pure Python, no emulator. See `docs/floppy.md`.
  - `tests/fixtures/` — files copied into `tests/sandbox/` before each Phosphoric
    test run, plus checked-in test images/expected `.bin`s for
    `test_pictconv.py`, `tests/fixtures/ttf_test_font.h` (a pre-generated
    font header so `test_hires.sh` doesn't depend on a system font), and
    `tests/fixtures/music.pt3`/`music_effects.pt3` (small hand-built
    synthetic PT3 modules, not real tunes, for `pt3.c`'s decode-correctness
    tests — see `docs/pt3.md`). Phosphoric needs `--loci-flash tests/sandbox`
    (not just bare `--loci`) for `pt3_load()`'s `file_load()` to actually
    find them.
  - `tests/sandbox/`, `tests/out/` — gitignored scratch, regenerated per run.
- `assets/` — real demo assets consumed by `src/section_*.c` files (as opposed
  to `tests/fixtures/`, which only test scaffolding uses). `assets/bird.h` —
  7-frame walk-cycle sprite data for `src/section_bird.c`, adapted from
  mihai-dragan's `oric_BAS` project (MIT License), github.com/xahmol/sprites
  — see that file's header comment for the full attribution note.
- `oscar64manual.md` — Oscar64 compiler reference; `docs/` — per-library API
  reference (`docs/README.md` is the index); consult before re-deriving
  Oscar64 compiler behavior or library APIs from scratch.

## Notes

- `src/buildtest.c`/`src/hires_test.c`/`src/floppy_test.c` are build-chain
  smoke tests proving each runtime end-to-end (`make test`/`make test-hires`/
  `make test-disk`), not demo content. `src/main.c` (+ `src/section_*.c`) IS
  the real demo content now, built for both distribution targets — extend
  it with more `src/section_*.c` effect modules as the demo grows.
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
- The floppy-disk target has a real, narrow, unresolved discrepancy: the AY
  mixer register computed by `pt3_tick()` differs from the tape/LOCI
  target's value for the same fixture, despite identical loaded module data
  and identical persistent channel state — see `docs/floppy.md`'s "Known
  issues" section before assuming PT3 playback is bit-identical across both
  targets.
