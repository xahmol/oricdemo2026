# Changelog

All notable changes to this project are documented in this file.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [1.0.0] — 2026-07-15

Initial public release — a full 12-section demoscene production for the
Oric Atmos, shipped as two independent, fully-playable distributions.

### Added

- Full 12-section running order: idi8b splash, Oric logo + raster bars,
  bird scene (parallax sky/creek backdrop with an animated bird), HIRES
  shapes showcase, polygon workout, 3D function surface, sprite showcase
  (satellite drifting over a procedural starfield), scroll showcase, wave
  showcase, macaw showcase, raster IRQ showcase, and a closing credits
  scroller — see [README.md](README.md#the-demo) for a screenshot per
  section and [docs/architecture.md](docs/architecture.md) for the full
  technical writeup.
- Two distribution targets built from the same source: a LOCI-backed
  tape target (`oricdemo.tap` + 7 asset files, all loaded at runtime) and
  a fully self-contained bootable Microdisc floppy image
  (`oricdemo_floppy.dsk`) needing no LOCI/DOS at all.
- Real AY-3-8912 music via an Arkos Tracker (`.aky`) player, alternating
  two tracks automatically once each finishes a full playthrough.
- A from-scratch Oric HIRES graphics library (`include/hires.c/h`), a
  proportional bitmap-font renderer (`include/ttf.c/h`), sprite/dissolve/
  raster-IRQ effect libraries, and Python asset-conversion tooling
  (`oric_pictconv.py`, `oric_ttfconv.py`, `oric_floppybuilder.py`) — see
  [docs/README.md](docs/README.md) for the full library API reference.
- An automated Phosphoric-based regression suite (`make test`,
  `make test-hires`, `make test-disk`) plus pure-Python unit tests for
  the picture/floppy-image conversion tools.
- Release packaging (`make zip`/`make usb`) laying out both distribution
  targets and this README (as a PDF) ready to copy onto a LOCI device or
  distribute directly.

### Notable fixes made before this release

- A real-hardware-only LOCI bug where bare filenames failed to resolve
  against the boot directory on multi-drive LOCI hardware, despite
  passing every automated/emulator test — fixed via `include/homedir.c/h`
  (no emulator currently reproduces this class of bug; see that file's
  header comment before adding new bare-filename LOCI file access).
- Two real Oscar64 `-O2` compiler miscompilation bugs found and worked
  around during development (documented at their exact call sites in
  `src/`/`include/`).
- A from-scratch Arkos Tracker (`.aky`) player replacing an earlier PT3
  player, after several rounds of PT3 decode-bug fixes still didn't
  produce satisfying music (PT3 support is archived on the `pt3` branch).
