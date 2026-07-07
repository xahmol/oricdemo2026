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

Build chain (see `Makefile`):
```
make            # compile -> build/oricdemo.bin (Oscar64) -> build/oricdemo.tap (tools/mktap.py)
make run        # launch build/oricdemo.tap in Oricutron (needs ORICUTRON_HOME)
make test       # Phosphoric headless boot smoke test (needs PHOSDIR in .env)
make test-capture CYCLES=N TYPEKEYS='...'   # calibration helper, dumps RAM+screenshot, no assertions
make usb        # copy build/oricdemo.tap to USBPATH (set in .env)
make docs       # README.md -> README.pdf (needs pandoc)
make zip        # release ZIP (build/oricdemo.tap + README.pdf)
make clean
```
`OSCAR64_HOME` defaults to `~/oscar64`, `ORICUTRON_HOME` to `~/oricutron` if unset.
`USBPATH` and `PHOSDIR` are read from `.env` (gitignored; copy `.env.example` to
`.env` and fill in values — see that file for WSL2 USB-mount notes).

## Source layout

- `src/main.c` — entry point. Currently a build-chain smoke test: inits the
  libraries below and reports LOCI/IJK detection status on screen.
- `src/strings.h` / `src/strings_en.h` — localisation gateway (`LANG=FR` ->
  `-dLANG_FR` selects `strings_fr.h`, not yet created). Only holds the two
  `MSG_*` strings `include/loci.c` needs; add app strings here as the demo grows.
- `include/` — the reusable Oric/Oscar64 library, copied from `OricScreenEditorLOCI`
  (see `libmanual.md` for the full API reference):
  - `oric_crt.c` + `crt_math.c` — custom Oscar64 runtime; **required**, not optional,
    for any Oscar64 build targeting the Oric (see the memory-layout comment at the
    top of `oric_crt.c` — startup region, char-set RAM, no IRQs).
  - `oric.h` — hardware registers/constants (VIA, AY-3-8912, screen RAM, char-set
    RAM, attribute codes).
  - `keyboard.c/h` — polled matrix keyboard scanner (no ROM/IRQ).
  - `charwin.c/h` — character-window drawing/cursor/viewport library.
  - `charset.c/h` — char-set RAM bank manipulation.
  - `ijk.c/h` — Raxiss IJK joystick interface.
  - `loci.c/h` — LOCI storage-device driver (detection, file I/O, overlay RAM).
    Degrades gracefully when no LOCI device is attached (`loci_present()` returns
    false; don't gate the whole program on it unless the demo actually needs
    LOCI-backed storage — unlike OricScreenEditorLOCI, this project does not
    require LOCI to boot).
- `tools/mktap.py` — wraps an Oscar64 raw `.bin` in an Oric `.tap` tape header.
- `tests/` — Phosphoric-based headless testing:
  - `tests/scripts/oric_screen.py` — decodes the 40x28 text screen out of a
    `--dump-ram-at` RAM dump (`$BB80`, strips serial-attribute bytes).
  - `tests/scripts/test_boot.sh` — the `make test` boot smoke test; asserts the
    status lines from `src/main.c` render.
  - `tests/fixtures/` — files copied into `tests/sandbox/` before each test run
    (currently empty; add binaries here if a test needs LOCI-flash-served files).
  - `tests/sandbox/`, `tests/out/` — gitignored scratch, regenerated per run.
- `oscar64manual.md`, `libmanual.md` — reference docs; consult before re-deriving
  Oscar64 compiler behavior or library APIs from scratch.

## Notes

- `src/main.c` is a placeholder proving the chain end-to-end (verified via
  `make test`), not demo content — replace it with actual effects.
- The `include/` library files are shared with `OricScreenEditorLOCI` and
  `locifilemanager-v2` but have already diverged between those two (they are
  copies, not a shared package) — don't assume changes here propagate, or that
  changes there apply here.
