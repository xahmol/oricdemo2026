# Changelog

All notable changes to this project are documented in this file.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [1.1.0] — 2026-07-17

### Added

- AY-3-8912 "digidrums"-style voice-sample playback (`include/voice.c/h`,
  `tools/oric_voiceconv.py`): two spoken clips, "Welcome to Oric Atmos"
  (after the Oric logo loads) and "Thanks for watching" (before the
  credits scroller starts), each converted from a text-to-speech WAV
  source down to 4-bit AY volume-register samples and played back via a
  paced VIA Timer 1 rewrite loop. The two clips share one fixed
  overlay-RAM address and use independent, per-clip sample rates (the
  thanks clip runs at a higher 7000Hz for better consonant clarity,
  within the same shared byte-size ceiling). See
  [docs/voice.md](docs/voice.md) for the full design writeup, including
  the real Oscar64-optimizer and BSS-budget issues found and fixed while
  building it. Playback technique based on ChibiAkumas's Z80 tutorial
  series, [Lesson P35](https://www.chibiakumas.com/z80/platform4.php#LessonP35).

### Fixed

- A visible flash-to-white-on-black during the outgoing scene's own
  transition wipe (`transition_clear()` swept left-to-right, blanking
  each row's ink/paper attribute bytes before the rest of that row was
  wiped, exposing the ULA's per-scanline hardware default for the rest
  of the transition) — now sweeps right-to-left instead.
- A permanent black stripe at column 0 of the bird scene's sky/bank/river
  rows (`section_background.c` used ink-then-paper column order; the ULA
  resets to hardware-default black paper at the start of every scanline,
  so column 0 never showed the intended non-black paper) — fixed by
  writing paper at column 0, ink at column 1.
- The same CRT-beam-racing class of bug in `hb_rect_fill()`'s large
  full-width fills, now filled right-to-left within each row so the
  ink/paper attribute bytes are touched last, not first.
- The bird scene not resetting its own animation state
  (`bird_tick_count`/`bird_frame`/`bird_col`/`bird_angle`) between
  passes through the demo's section loop — caused both a wrong starting
  position and a genuine garbled-pixel artifact on every run after the
  first. Every other section was audited for the same bug class; none
  found.
- Two pre-existing `make test-hires` failures (missing charset data and
  a missing include path in the test fixture, not the library itself) —
  75/75 assertions now pass.

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
