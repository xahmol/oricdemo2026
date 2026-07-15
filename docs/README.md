# oricdemo2026 — Library Reference Manual

Oscar64 bare-metal libraries for the Oric Atmos.
Target: 6502A, 1 MHz, no ROM calls.

For how these libraries fit together into the actual demo (memory maps,
the section sequencer, per-section techniques), see
[architecture.md](architecture.md) instead — this manual is the
`include/` function-level API reference.

Split into one file per library (originally a single `libmanual.md`,
adapted from [OricScreenEditorLOCI](https://github.com/xahmol/OricScreenEditorLOCI)'s
`libmanual.md`, itself adapted from
[locifilemanager-v2's `libmanual.md`](https://github.com/xahmol/locifilemanager-v2/blob/main/libmanual.md)).
Content specific to those sibling projects' own application code (their
menu systems, character editors, etc.) has been dropped — this manual only
documents what's actually present in `include/` here.

## Contents

1. [Hardware overview (oric.h)](oric.md)
2. [Keyboard scanner (keyboard.h)](keyboard.md)
3. [Character window library (charwin.h)](charwin.md)
4. [IJK joystick (ijk.h)](ijk.md)
5. [LOCI mass-storage API (loci.h)](loci.md)
6. [Generic charset library (charset.h)](charset.md)
7. [HIRES graphics library (hires.h)](hires.md)
8. [TTF font rendering (ttf.h)](ttf.md)
9. [3D vector/matrix math (vector3d.h)](vector3d.md)
10. [Fixed-point sine/cosine table (fixedmath.h)](fixedmath.md)
11. [Sprite system (sprite.h)](sprite.md)
12. [Fade/dissolve transitions (dissolve.h)](dissolve.md)
13. [Raster IRQ / mid-frame effects (rasterirq.h)](rasterirq.md)
14. [AY-3-8912 register-write helper (ay.h)](ay.md)
15. [Arkos Tracker music player (arkos.h)](arkos.md)
16. [Runtime picture loading (picture.h)](picture.md)
17. [Image conversion (tools/oric_pictconv.py)](pictconv.md)
18. [Floppy-disk build target (tools/oric_floppybuilder.py, floppy.h)](floppy.md)
19. [Build notes](build-notes.md)
20. [Architecture — memory maps, section sequencer, subsystem design](architecture.md)
