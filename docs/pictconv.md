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

Implemented so far: `mono`, `colored`, `aic`, `samhocevar`, `pictoric`
(the last is a port of a *different* tool, Samuel Devulder's PictOric, not
OSDK's own pictconv — see its own section below). Not implemented (OSDK
pictconv modes only, from here on), with the actual reason for each
(checked against pictconv's source, not assumed):

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
  --mode {mono,colored,aic,samhocevar,pictoric}  (default: mono)
  --dither {none,ordered,floyd-steinberg}    (default: floyd-steinberg; ignored by samhocevar/pictoric)
  --format {bin,header}                      (default: bin)
  --label NAME        C array name for --format header (default: oric_image)
  --ink/--paper NAME-OR-0-7      mono mode ink/paper (default: white/black)
  --aic-ink0/--aic-paper0/--aic-ink1/--aic-paper1
                       aic mode even/odd-row ink/paper (default: white/black, cyan/black)
  --samhocevar-depth N  samhocevar mode recursive lookahead depth (default: 2)
  --no-inverse-attr    samhocevar/pictoric: disable the "inverse attribute
                       byte" search candidates (see that section below)
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

The full 34-command search includes upstream's "inverse attribute byte"
trick (writing an ink/paper-change byte with bit7 set). An earlier version
of this port removed those 16 candidates on the assumption the mechanism
was unconfirmed on real Oric hardware — that assumption was WRONG: bit7 on
an attribute byte is a real, confirmed ULA behaviour (it inverts only that
one byte's own displayed cell, without changing what ink/paper actually
gets set going forward — see `include/oric.h`'s HIRES bitmap section for
the confirming source). Restored once verified.

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

## PictOric mode

A port of a *different* tool from OSDK's own pictconv: Samuel "__sam__"
Devulder's [PictOric](https://github.com/Samuel-DEVULDER/PictOric)
(`PictOric.lua`, v1.2, 2019-2020) — a GrafX2-script / command-line Oric
image converter, independently developed, with a genuinely different
(and in two real ways, better) algorithm than every mode above:

1. **Correct sRGB linearisation.** Every mode above (including
   `samhocevar`) quantizes and diffuses error directly in sRGB
   (gamma-encoded) space — a real, if usually minor, inaccuracy. PictOric
   converts to linear light first (the standard sRGB inverse-EOTF
   formula), does all colour-distance and error-diffusion maths there,
   matching how the eye and the actual analogue video signal behave.
2. **A genuinely optimal per-scanline search**, not just a deeper
   lookahead. The key insight ("neglect cross-octet error"): if each
   6-pixel block's own dithering error is computed using ONLY the error
   carried in from the row *above* (never from a preceding block in the
   *same* row), the total remaining error from block `x` to the end of
   the row depends only on `(x, current ink, current paper)` — a small,
   memoizable state space (40 blocks × 8 × 8 = 2560 states). That makes a
   full recursive search over the *entire* row's ink/paper-change
   sequence cheap and exactly optimal for that row, rather than
   `colored` mode's budget-limited backtracking or `samhocevar`'s
   fixed-small-depth lookahead. It's also why it's fast in practice —
   typically a few seconds per image, not minutes.

Close to a line-by-line port for the parts that matter (the sRGB
formulas, the "redmean"-style perceptual distance PictOric calls
`dist2_alg=1` — its own default — the 256-entry Ostromoukhov
variable-coefficient error-diffusion table, extracted programmatically
from the Lua source rather than hand-typed, and the `calcErr`/`findRec`
recursive search itself). **Also** ported (this was originally a "reuse
this project's shared loader" simplification — see the byte-for-byte
verification section below for why that turned out to matter more than
expected): `load_and_fit_linear()`, a close port of PictOric's own
`to_screen()`/`getLinearPixel()` — a box-filter average done in LINEAR
light space, not gamma-encoded sRGB. This project's shared `load_and_fit()`
(used by every other mode) resizes via PIL's LANCZOS filter directly on
gamma-encoded bytes, then linearises after resizing — a real, if usually
minor, difference from averaging light values themselves.

Deliberately **not** ported: PictOric's own AIC mode (`--mode aic` above
already covers that use case), its CIE-Lab-based alternative distance
metrics (not the tool's own default), its "auto blank margin" heuristic
(a one-off tuned for a specific title-screen image, not general-purpose),
and its BMP/TAP file writer and GrafX2 integration (irrelevant — this
project has its own CLI/output format).

`--dither` is ignored (same as `samhocevar` — the algorithm has its own
built-in diffusion). `--no-inverse-attr` is shared with `samhocevar`, see
the next section for why it exists.

## The "inverse attribute byte" trade-off, and `--no-inverse-attr`

Both `samhocevar` and `pictoric` rely on a real, hardware-confirmed
mechanism: writing an ink/paper-change attribute byte with bit7 set makes
that byte's own displayed cell show the *complement* of the new colour,
without changing what ink/paper actually gets set going forward (see
`include/oric.h`'s HIRES bitmap section). Both algorithms use this to
"disguise" an attribute change as a colour the current block's content
already needs, while quietly setting up a *different* ink/paper state for
upcoming blocks.

Comparing this project's `samhocevar`/`pictoric` output against a real
published reference conversion (see "Verifying against upstream
references" below) surfaced a genuine, concrete trade-off: on a real
photographic portrait, both modes occasionally used this trick to lock an
entire multi-block stretch of a row onto a badly-mismatched colour pair
(observed: a stretch of warm skin/hair tones rendered using {white,
cyan} — a jarring, clearly-wrong-looking result). Root cause, confirmed
directly (not guessed): both algorithms' speed comes from a
"neglect cross-block/cross-octet error" simplification — each block's own
cost estimate assumes a fixed incoming error state from the row *above*
only, never accounting for how a poor colour choice compounds across the
many real pixels forced to use it in the blocks *after* it. A direct,
non-recursive check confirmed the naive total error for the affected row
segment was roughly **4x worse** under the colour pair the search actually
picked than under the obviously-better alternative — yet the search
(correctly, given its own simplified cost model) still preferred it,
because the model doesn't know how badly that choice will compound.

This is **not a bug to silently patch** — the inverse-attribute mechanism
is real, and disabling it is a genuine reduction in search flexibility,
not a pure improvement. It's exposed as an opt-out instead: pass
`--no-inverse-attr` to drop those candidates from the search entirely.
Confirmed to eliminate the artifact on the same troublesome reference
photo, at the cost of a smaller solution space. Try it if a conversion
shows an unexpected, wrong-hued streak that a mono/aic pass on the same
image doesn't show.

## Verifying against upstream references

Both `samhocevar` and `pictoric` were checked against real, independently-
published reference conversions, not just against each other:

- **OSDK's own documentation page**
  (`osdk.org/index.php?page=documentation&subpage=pictconv`) publishes a
  source photo ("Buffy") alongside OSDK pictconv's own official `-f6`
  output (`buffy_f6.png`). Fetching both and running this project's
  `samhocevar`/`pictoric` against the same source photo confirmed: OSDK's
  *own* official `-f6` reference is itself streaky/noisy on this
  photographic portrait (not a clean result) — validating that the
  noise seen on real photos throughout this project's own testing is
  inherent to the technique/source-image mismatch, not a sign of a
  broken port. `pictoric`'s result was notably closer in overall
  character (colour balance, streak pattern, recognizability) to OSDK's
  official reference than this project's own `samhocevar` port managed,
  a real, useful data point in `pictoric`'s favour for photographic
  subjects specifically.
- **A hardware-behaviour correction found along the way**: verifying
  `samhocevar` against real conversions (both OSDK's and PictOric's own,
  which also relies on the same mechanism) surfaced a genuine mistake in
  an earlier version of this project's own work — see `samhocevar`'s own
  section above and `include/oric.h`'s HIRES bitmap section for the full
  correction (an "inverse attribute byte" mechanism previously assumed
  unconfirmed on real hardware, which turned out to be real and
  documented after all).
- **The cyan-streak finding**: this same Buffy comparison is what first
  surfaced the "inverse attribute byte" trade-off documented in its own
  section above — a user noticed the streaks in this project's own
  conversion didn't match OSDK's reference, which prompted tracing the
  actual cause rather than assuming it was ordinary photo-conversion
  noise. Confirmed present in BOTH `samhocevar` and `pictoric`
  (independently-implemented algorithms), confirmed via a direct,
  non-recursive error calculation (not just visual impression), and
  confirmed fixable via `--no-inverse-attr`.
- **Running the real upstream PictOric.lua directly** (not just reading
  its source): built Lua 5.4 from source (no system package available
  without `sudo`) and ran the actual, unmodified `PictOric.lua` against
  the same source photos this project's own port was tested against,
  diffing the real output byte-for-byte against `pictoric` mode's own
  result. This is what the two fixes below came from — reading source
  code alone would not have caught either.
  - **Resize-domain fix**: PictOric's own `to_screen()`/`getLinearPixel()`
    box-filter-averages in LINEAR light space; this project's shared
    `load_and_fit()` (used by every other mode) resizes via PIL's LANCZOS
    filter directly on gamma-encoded bytes, linearising only afterward.
    `pictoric` mode now has its own `load_and_fit_linear()` matching
    upstream's approach exactly, instead of reusing the shared loader —
    confirmed via the byte diff that this was necessary for the first
    several rows to match at all.
  - **Padding-bleed fix, a real, visible bug**: on a source image needing
    letterbox padding (aspect ratio narrower than 240x200), a colour pair
    chosen for real content just before the padding began (e.g. `{ink=red,
    paper=white}`, a good match for bright/warm content) was left active
    straight into the padding region, which is genuinely pure black —
    and red turns out to be numerically *closer* to black than white is,
    so the padding rendered as a solid red streak instead of clean black.
    This is the SAME root cause as the cyan-streak finding (the "neglect
    cross-octet error" cost model doesn't account for how a colour choice
    compounds across many subsequent blocks), but via a plain DIRECT
    attribute change this time — `--no-inverse-attr` can't catch it, since
    no inversion is involved. Confirmed by comparing against the real
    upstream tool's own clean-black output on the identical row. Fixed
    with a **targeted, low-risk special case**, not a change to the core
    search: letterbox padding is always exactly `(0,0,0)` (a box-filter
    average of real photo content essentially never produces this by
    chance), so any row's *trailing* run of all-`(0,0,0)` blocks is
    detected up front and excluded from the recursive search entirely,
    then forced to render as clean black afterward (inserting one
    paper-change-to-black attribute byte if needed) — real image content
    is completely unaffected, since the detection is exact-equality, not
    a threshold.
  - **What remained unexplained, and was left as-is**: even after both
    fixes, ~60% of bytes still differ from the real reference on a busy
    photographic source — tracing the very first divergence (a single
    flipped pixel bit, row 4 of the same test image) showed the
    underlying colour-distance comparison was genuinely a near-exact tie
    (within roughly 1%) on both sides, just resolving to opposite
    outcomes. Given this algorithm makes many thousands of chained,
    interdependent near-tie floating-point comparisons per image, a tiny
    difference at any single point (from a rounding difference in some
    intermediate step, not necessarily a logic bug) cascades through
    later error-diffusion and search decisions and amplifies into
    visibly different, though not necessarily *worse*, output. This
    project's own regression fixtures use small synthetic images with
    no near-ties (flat colours, hard edges) specifically so they stay
    deterministic and byte-exact — real photographic source content does
    not have that property, in either implementation.

## Testing

`tests/scripts/test_pictconv.py` (run via `make test-pictconv`) is a
pure-Python, no-emulator unit test: it runs the converter against small
checked-in synthetic test images (`tests/fixtures/hires_test_input.png`,
`hires_test_stripes.png`) and diffs the output against checked-in expected
`.bin` fixtures, including a hand-verified case that exercises the
attribute optimizer across a 2-colour boundary block.
