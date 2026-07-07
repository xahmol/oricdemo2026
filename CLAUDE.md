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

**Two runtimes, two programs.** `include/oric_crt.c` (default) and
`include/oric_crt_hires.c` (HIRES mode) are separate, incompatible region layouts —
see [docs/hires.md](docs/hires.md#memory-layout--the-oric_crt_hiresc-runtime) for why
(the HIRES bitmap needs `$9800-$BFDF`, which the default runtime uses for
code/data/stack). `src/main.c` builds with the default runtime; `src/hires_test.c`
builds with the HIRES runtime. Never mix them for the same program.

Build chain (see `Makefile`):
```
make            # compile -> build/oricdemo.bin (Oscar64) -> build/oricdemo.tap (tools/mktap.py)
make run        # launch build/oricdemo.tap in Oricutron (needs ORICUTRON_HOME)
make test       # Phosphoric boot smoke test + oric_pictconv.py unit tests
make test-hires # opt-in: HIRES library Phosphoric smoke test (separate .tap, oric_crt_hires.c)
make test-pictconv # oric_pictconv.py unit tests alone (pure Python, no emulator)
make test-capture CYCLES=N TYPEKEYS='...'   # calibration helper, dumps RAM+screenshot, no assertions
make usb        # copy build/oricdemo.tap to USBPATH (set in .env)
make docs       # README.md -> README.pdf (needs pandoc)
make zip        # release ZIP (build/oricdemo.tap + README.pdf)
make clean
```
`OSCAR64_HOME` defaults to `~/oscar64`, `ORICUTRON_HOME` to `~/oricutron` if unset.
`USBPATH` and `PHOSDIR` are read from `.env` (gitignored; copy `.env.example` to
`.env` and fill in values — see that file for WSL2 USB-mount notes). Python tooling
(`tools/oric_pictconv.py`, `tools/oric_ttfconv.py`) needs Pillow:
`pip install -r tools/requirements.txt`.

## Source layout

- `src/main.c` — TEXT-mode entry point (default runtime). Currently a build-chain
  smoke test: inits the TEXT-mode libraries below and reports LOCI/IJK detection
  status on screen.
- `src/hires_test.c` — HIRES-mode entry point (`oric_crt_hires.c` runtime). Growing
  test fixture for `include/hires.c`/`ttf.c`, exercised by `make test-hires` —
  not demo content, see `tests/scripts/test_hires.sh` for what's asserted.
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
  - `hires.c/h` — HIRES-mode bitmap graphics library (pixel/line/shape/bitblit/
    text primitives, mode switching, ink/paper attributes, AIC). Requires the
    `oric_crt_hires.c` runtime. See `docs/hires.md`.
  - `ttf.c/h` — proportional bitmap-font rendering on top of `hires.c`, fed by
    `tools/oric_ttfconv.py`. See `docs/ttf.md`.
  - `vector3d.c/h` — 3D vector/matrix math, copied verbatim from Oscar64's own
    `include/gfx/`. Not currently used by any demo code. See `docs/vector3d.md`.
- `tools/mktap.py` — wraps an Oscar64 raw `.bin` in an Oric `.tap` tape header.
- `tools/oric_pictconv.py` — JPG/PNG -> HIRES bitmap converter (mono/colored/aic
  modes). See `docs/pictconv.md`.
- `tools/oric_ttfconv.py` — TTF -> proportional HIRES bitmap font converter, for
  `include/ttf.h`. See `docs/ttf.md`.
- `tools/requirements.txt` — Pillow, needed by both Python converters above.
- `tests/` — Phosphoric-based headless testing:
  - `tests/scripts/oric_screen.py` — decodes the 40x28 TEXT screen, or hex-dumps
    arbitrary bytes (`--bytes ADDR:LEN`), out of a `--dump-ram-at` RAM dump.
  - `tests/scripts/test_boot.sh` — the `make test` boot smoke test (TEXT mode,
    `src/main.c`); asserts the status lines render.
  - `tests/scripts/test_hires.sh` — the `make test-hires` smoke test (HIRES mode,
    `src/hires_test.c`); byte-exact assertions via `oric_screen.py --bytes`.
  - `tests/scripts/test_pictconv.py` — `oric_pictconv.py` unit tests (`make
    test-pictconv`), pure Python, no emulator.
  - `tests/fixtures/` — files copied into `tests/sandbox/` before each Phosphoric
    test run, plus checked-in test images/expected `.bin`s for
    `test_pictconv.py` and `tests/fixtures/ttf_test_font.h` (a pre-generated
    font header so `test_hires.sh` doesn't depend on a system font).
  - `tests/sandbox/`, `tests/out/` — gitignored scratch, regenerated per run.
- `oscar64manual.md` — Oscar64 compiler reference; `docs/` — per-library API
  reference (`docs/README.md` is the index); consult before re-deriving
  Oscar64 compiler behavior or library APIs from scratch.

## Notes

- `src/main.c`/`src/hires_test.c` are build-chain smoke tests proving each
  runtime end-to-end (`make test`/`make test-hires`), not demo content —
  replace them with actual effects.
- The TEXT-mode `include/` library files (`charwin`, `keyboard`, `charset`,
  `ijk`, `loci`) are shared with `OricScreenEditorLOCI` and `locifilemanager-v2`
  but have already diverged between those two (they are copies, not a shared
  package) — don't assume changes here propagate, or that changes there apply
  here. `hires.c/h`, `ttf.c/h`, `oric_crt_hires.c`, and the Python tools are
  original to this project, not carried over from those siblings.
- HIRES-mode programs get **less usable RAM** than TEXT-mode ones (~36.1 KB vs.
  ~42.4 KB code/data/bss) — see `docs/hires.md`'s memory-layout table before
  assuming the default runtime's budget applies.
