# Image conversion (tools/oric_pictconv.py)

Converts a JPG/PNG source image into HIRES bitmap data, loadable directly
at `HIRESVRAM` (e.g. via `loci_load`/`file_load`, see [loci.md](loci.md), or
`#embed`). Requires Pillow (`pip install -r tools/requirements.txt`, shared
with `oric_ttfconv.py`).

**Provenance:** written in the spirit of the OSDK's C++
[`pictconv`](https://github.com/Oric-Software-Development-Kit/osdk/tree/master/osdk/main/pictconv)
tool — same byte format and the same per-scanline colour-attribute
optimisation idea (`colored` mode, below) — but is a fresh Python
implementation, not a port of that tool's C++ structure.

Implemented so far: `mono`, `colored`, `aic`, `samhocevar`. Not implemented,
with the actual reason for each (checked against pictconv's source, not
assumed):

- **`-f5`/`-f5z` charmap**: genuinely out of scope — it converts to a
  *character set + text-screen* representation (TEXT mode), not a HIRES
  bitmap at all.
- **`-f3` twilight mask**: repurposes bits 6/7 for a bespoke sprite-
  transparency-mask scheme specific to one external engine ("TwilightE"),
  incompatible with the standard bit6="pixel data"/bit7="invert" convention
  this project's HIRES bytes use everywhere else (`include/hires.h`) —
  output from this mode isn't a displayable HIRES bitmap under that
  convention at all, so there's nothing for `hb_*`/`hires.c` to consume.
- **`-f2` RGB and `-f4` RB (scanline colour-channel decomposition)**:
  genuinely valid, general-purpose Oric HIRES techniques, same as
  `mono`/`colored`/`aic`/`samhocevar` — **not implemented yet for scope/time
  reasons, not because they're irrelevant**. RGB/RB write a raw INK
  attribute (`R`/`G`/`B` cycling every 3 rows for RGB, `R`/`GB` every 2 rows
  for RB) at the start of each scanline, then encode per-pixel brightness as
  ink-on/paper-off for that row's single colour channel — relying on real
  composite/RGB CRT scanline bleed to perceptually blend adjacent
  channel-rows into a fuller colour image (conceptually similar to `aic`'s
  2-line ink/paper alternation, but 3-channel and reliant on analogue
  blending that Oricutron/Phosphoric don't reproduce, so it won't look right
  on an emulator screenshot). Flag if you'd like these added.
- **`-f6` Sam Hocevar dithering ("img2oric")**: implemented as `samhocevar`,
  see its own section below.

## CLI

```
python3 tools/oric_pictconv.py INPUT OUTPUT
  --mode {mono,colored,aic,samhocevar}       (default: mono)
  --dither {none,ordered,floyd-steinberg}    (default: floyd-steinberg; ignored by samhocevar)
  --format {bin,header}                      (default: bin)
  --label NAME        C array name for --format header (default: oric_image)
  --ink/--paper NAME-OR-0-7      mono mode ink/paper (default: white/black)
  --aic-ink0/--aic-paper0/--aic-ink1/--aic-paper1
                       aic mode even/odd-row ink/paper (default: white/black, cyan/black)
  --samhocevar-depth N  samhocevar mode recursive lookahead depth (default: 2)
```

`--format bin` writes a flat 8000-byte stream matching `HIRESVRAM`'s
layout exactly. `--format header` writes a plain `const unsigned char
NAME[8000] = {...};` C array (no `#embed` needed since the Python tool
already has the bytes).

Colour arguments accept either a palette name (`black red green yellow
blue magenta cyan white`) or a numeric index `0`-`7`.

## The Oric's 8-colour palette

A pure 3-bit RGB cube (no C64-style shading — the AY-derived colour signal
is digital, not analogue): each channel is either fully off or fully on.
Palette index matches the `A_FW*`/`A_BG*` colour code in
[oric.md](oric.md) (`0`=black ... `7`=white).

## Dithering

Three options, in order of recommended use:

1. **`floyd-steinberg`** (default) — standard error-diffusion quantization
   against the active colour set. The primary quality option.
2. **`ordered`** — a simple 4×4 Bayer threshold matrix. Cheaper, different
   look. *Not* an attempt to reproduce pictconv's own hand-tuned 9-level
   dither mask — that's a proprietary artifact of the original tool, not
   required for a good result here.
3. **`none`** — direct nearest-colour quantize. Useful for already-flat
   source images (icons, logos) or debugging.

## Mono mode

A single global ink/paper pair for the whole image, no per-line attribute
changes — the simplest, cheapest mode. If `--ink`/`--paper` are left at
their defaults (white/black, the ULA's own per-scanline reset state), the
output needs **zero** attribute bytes anywhere in the image.

## Colored mode — the attribute-placement optimizer

Per-scanline backtracking search that decides, block by block (each HIRES
byte = 6 source pixels), whether to keep the current ink/paper state,
exploit the invert-bit trick, or spend a block on an attribute-change byte
— adapted from (not a literal port of) pictconv's `BlocOf6`/`RecurseLine`
algorithm (`oric_converter.h`/`oric_converter_colored.cpp`), with two
deliberate differences:

1. Attribute changes are tried for **both** ink and paper (iterating all 8
   candidate colours for each), where the original only ever changes PAPER
   via attribute and treats ink changes as available solely by chance.
   This is a strict generalisation — it can only find equal-or-better
   solutions.
2. The original's "change paper using inverse video" branch (writing the
   attribute byte itself with bit7 set) is dropped: it reaches the same
   *future* colour state as directly writing the plain attribute for the
   complementary colour (already covered by trying all 8 candidates), and
   its exact effect on the attribute byte's own displayed cell isn't
   confirmed by this project's hardware research — not worth the
   uncertainty for a purely cosmetic edge case.

A block using more than 2 distinct colours is unrepresentable exactly (a
HIRES byte only has 2 "slots"); such a block's 3rd+ colour is silently
folded into whichever of the first two colours it isn't, degrading
gracefully rather than erroring — proper dithering keeps this rare. If the
per-line search can't find a solution (or a per-line attempt budget runs
out, guarding against pathological inputs), that line falls back to a
plain per-block bit pattern with no attribute changes at all — colour-
incorrect for that line, but always renders *something*.

## AIC mode

A different `{ink, paper}` pair on even vs. odd scanlines (see
[hires.md](hires.md)'s `HiresAIC`) — a **flat** per-parity palette (matching
pictconv's own `m_PaletteAIC[2][2]`), not the block optimizer run within
each parity group. Columns 0-1 of every row hold that row's INK/PAPER
attribute pair, baked directly into the output data (a self-contained
blob — no separate `hires_aic_apply_range()` runtime call needed), costing
the first 12 pixels of every scanline — the same documented hardware
constraint colored mode's attribute insertions run into.

## Sam Hocevar mode ("img2oric", upstream `-f6`)

A much more thorough, much slower alternative to `colored`/`aic`'s own
per-scanline optimizers: a per-6-pixel-block search with 2-block recursive
lookahead (`--samhocevar-depth`, default 2 — matches upstream's own `DEPTH`;
3 is documented upstream as better still, slower still), scoring each of 34
candidate commands (ink/paper change, direct or inverse-video, or plain
pixel printing) against a gamma-corrected perceptual error metric, with full
Floyd-Steinberg-style error diffusion into the row below as well as
sideways — not just a flat nearest-colour dither like `mono`/`aic` use.

Unlike `colored`/`aic` above (adapted "in the spirit of" pictconv's C++,
not literal ports), this **is** a close, near-line-by-line port of
`oric_converter_samhocevar.cpp` (Sam Hocevar, "img2oric", 2008, WTFPL) —
the quality comes from its exact, empirically-tuned constants and
recursive structure, so approximating it loosely would lose the point of
having it. Two deliberate deviations from the upstream source (both safety
fixes, not behaviour changes — see the code's own header comment in
`tools/oric_pictconv.py` for the full detail): the dead, never-read
`maxerror` parameter is dropped, and the pixel buffer's edge padding is
sized generously enough to cover the recursion's own worst-case lookahead
distance, instead of upstream's tighter padding (which relies on C
pointer arithmetic silently reading past its own buffer's real bounds near
the right/left edges — undefined behaviour that Python's bounds-checked
lists won't tolerate).

**Expect real per-image runtimes in minutes, not seconds** — this is a
pure-Python translation of an algorithm upstream itself describes as
"much much slower than other methods," with no vectorization. `--dither`
is ignored in this mode (the algorithm has its own built-in diffusion).

**When it's actually worth reaching for**: not a blanket upgrade over
`mono`/`aic` — those already produce clean results on source art suited to
HIRES's constraints (flat regions, line art, existing hatching/dither, high
native contrast — see the "Art content" reasoning in the project's own demo
plan). Sam's method earns its cost specifically on trickier, more
continuous-tone source images where `mono`/`aic`/`colored` produce visible
banding or noise — try the cheaper modes first and only reach for
`samhocevar` if they fall short.

## Testing

`tests/scripts/test_pictconv.py` (run via `make test-pictconv`) is a
pure-Python, no-emulator unit test: it runs the converter against small
checked-in synthetic test images (`tests/fixtures/hires_test_input.png`,
`hires_test_stripes.png`) and diffs the output against checked-in expected
`.bin` fixtures, including a hand-verified case that exercises the
attribute optimizer across a 2-colour boundary block.
