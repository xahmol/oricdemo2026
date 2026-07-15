# HIRES graphics library (hires.h)

Bitmap drawing library for the Oric's HIRES mode. Mirrors the shape of
Oscar64's own `include/gfx/bitmap.h` (`~/oscar64/include/gfx/`), retargeted
to the Oric's actual HIRES hardware — see [oric.md](oric.md)'s "HIRES
bitmap" and "TEXT/HIRES mode-switch attributes" sections for the hardware
facts this library builds on. Include `hires.h`; it auto-compiles `hires.c`
via `#pragma compile`.

Two things make the Oric fundamentally different from a C64 bitmap:

1. **6 pixels per byte, not 8** (bits5-0, bit5 = leftmost; bit6 must stay
   set on every pixel byte or the ULA reads it as a colour attribute
   instead).
2. **No true per-pixel colour**: colour is a byte-level INK/PAPER attribute
   that applies to the rest of a scanline, plus a per-byte invert bit.
   There is no equivalent of `gfx/mcbitmap.h`'s per-pixel dithered colour.

## Memory layout — the `oric_crt_hires.c` runtime

Building any program that uses this library requires the **alternate**
Oscar64 runtime, `include/oric_crt_hires.c`, passed via
`-rt=include/oric_crt_hires.c` — not the default `include/oric_crt.c`. This
is the single most important thing to get right before writing any HIRES
code.

**Why:** the HIRES bitmap (`$A000-$BF3F`) plus its footer (`$BF68-$BFDF`)
and relocated charset banks (`$9800-$9FFF`) together occupy `$9800-$BFDF`.
The default `oric_crt.c` runtime's `main` (code/data/bss) region extends to
`$B200` and its `stack` region to `$B400` — both inside that range. Rather
than edit the shared `oric_crt.c` (used by `src/main.c` and by the sibling
projects this codebase was bootstrapped from), `oric_crt_hires.c` is a
parallel runtime with `main` shrunk to `$0580-$9600` and `stack` moved to
`$9600-$9800`:

| Region | `oric_crt.c` (default) | `oric_crt_hires.c` |
|---|---|---|
| `main` (code+data+bss) | `$0580-$B200` (~42.4 KB) | `$0580-$9600` (~36.1 KB) |
| `stack` | `$B200-$B400` | `$9600-$9800` |
| `$9800-$9FFF` | free (unused by either) | HIRES charset banks (reserved, uncovered by any region) |
| `$A000-$BFDF` | free (unused by either) | HIRES bitmap + footer (reserved, uncovered by any region) |

This is a real, ~6.3 KB budget cut for any HIRES-using program — a genuine
trade-off, not a bug. The Makefile builds two separate programs to keep
both runtimes independently testable: the default `make`/`make test`
(`src/main.c`, `oric_crt.c`) is completely unaffected by anything in this
section; `make test-hires` (`src/hires_test.c`, `oric_crt_hires.c`) is the
opt-in HIRES-specific build.

## `oric.h` additions

See [oric.md](oric.md) for the full constant tables (`HIRESVRAM`,
`HIRES_ROW_BYTES`, `HIRES_ROWS`, `HIRES_WIDTH_PX`, `HIRES_SIZE`,
`HIRES_FOOTER`, `HIRES_CHARSET_STD`/`ALT`, and the
`A_TEXT_*`/`A_HIRES_*` mode-switch attributes).

## `HiresBitmap` / `HiresClip` — canvas descriptor

```c
typedef struct {
    uint8_t *data;   // base address (HIRESVRAM for the live screen)
    uint8_t  rows;   // canvas height in scanlines (<=HIRES_ROWS for the live screen)
} HiresBitmap;

typedef struct {
    uint8_t top, left, bottom, right;   // clip bounds
} HiresClip;
```

Unlike `gfx/bitmap.h`'s `Bitmap` (which has a variable `cwidth` for
arbitrary off-screen buffers), a HIRES canvas is always 40 bytes wide (one
Oric scanline) — only the row count varies, e.g. for an off-screen buffer
smaller than the full 200-row screen.

## Initialisation

```c
void hires_init(void);
```
Builds the row/column addressing lookup tables (`hires_row_off[200]`,
`hires_col_byte[240]`, `hires_col_mask[240]` — 880 bytes total) via
repeated addition, mirroring `charwin_init()`'s "avoid a 6502
multiply/divide" approach. **Call once before any other `hires_*`/`hb_*`
function.**

This table-based approach (division-by-6 and modulo-by-6 replaced by
lookup tables, row addresses precomputed rather than multiplied at draw
time) isn't just this project's own preference — it's independently
confirmed as *the* standard Oric optimisation technique by OSDK's
["Efficient rasterization"](https://osdk.org/index.php?page=articles&ref=ART19)
article, which measures the ROM's naive `CURSET` pixel-plot routine at
4662 cycles/pixel vs. 85 cycles/pixel (54× faster) using this exact
Div6/Mod6/Scanlines-table scheme.

```c
void hb_init(HiresBitmap *hb, uint8_t *data, uint8_t rows);
```
Populate a canvas descriptor. For the live screen:
`hb_init(&hb, (uint8_t *)HIRESVRAM, HIRES_ROWS)`.

```c
void hb_fill(const HiresBitmap *hb, uint8_t value);
```
Fill an entire canvas with a raw byte value (e.g. `0x40` for all-paper).
**Real Oric RAM is NOT zero-initialized at power-on** — a fresh
`HiresBitmap`'s contents are garbage, not blank. Always fill before
drawing.

## Canvas scroll

```c
void hb_scroll_up(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_down(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_left(const HiresBitmap *hb, uint8_t amount, bool fill_set);
void hb_scroll_right(const HiresBitmap *hb, uint8_t amount, bool fill_set);
```

**Vertical** (`up`/`down`): a straight `memmove` of whole 40-byte rows —
correct regardless of overlap direction, since HIRES is simple linear
raster (no C64-style cell interleaving to worry about) — then the vacated
rows are `memset` to `fill_value`. **Horizontal** (`left`/`right`):
per-row, the row is snapshotted into a local pixel buffer via `hb_get`
(avoiding any in-place overlap entirely), shifted, the vacated edge filled,
then written back via `hb_put` — correctness-first, same approach as
`hb_bitblit`/`hb_put_chars`.

**These process every row of the `HiresBitmap` passed in** (`hb->rows`),
unlike the rect/fill primitives below (already scoped to their own `w`/
`h`) — scrolling only the live 200-row screen costs 200×240 pixel ops for
a horizontal scroll. Use a small sub-canvas (`hb_init()` with a narrower
`rows` count, pointed at a sub-range of the buffer) when you only need to
scroll part of the screen; this is also how `test_hires.sh` keeps its own
scroll-left/right tests fast under Phosphoric.

**`hb_bitblit` is NOT safe for overlapping same-canvas source/destination
regions** (no reverse-iteration handling like `gfx/bitmap.c`'s `BlitOp`) —
don't try to `hb_bitblit` a canvas onto itself to scroll it; use these
dedicated functions instead. A hand-unrolled absolute-addressing version
of vertical scroll is a known ~2× speedup on real Oric hardware (per OSDK
["Optimizing Buggy Boy (2)"](https://osdk.org/index.php?page=articles&ref=ART13),
which measures 9 cycles/byte for `lda $a000+40*1,x` / `sta $a000+40*0,x`
vs. 18 cycles/byte for indirect-indexed addressing) — not built here;
`memmove` is the correct-first default.

### Byte-aligned (fast) horizontal scroll

```c
void hb_scroll_left_fast(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
void hb_scroll_right_fast(const HiresBitmap *hb, uint8_t amount, uint8_t fill_value);
```

`hb_scroll_left`/`right`'s per-pixel `hb_get`/`hb_put` cost is too slow to
scroll a full-width canvas every frame (same order-of-magnitude problem
`sprite.h`'s `hb_bitblit`-vs-`hxspr_draw` tradeoff solves for sprites — see
that header). These shift each row's bytes by `amount` whole column-bytes
(6px granularity) via `memmove`, filling the vacated edge with the raw
`fill_value` byte — the same "byte-aligned = fast" approach, applied to
scrolling instead of sprite drawing. Used by `src/section_clouds.c`'s
parallax cloud layer.

**Caveat inherited from how this project uses column-bytes 0-1**: if your
canvas's rows have their own ink/paper attribute bytes at column-bytes 0-1
(see `hires_row_colors()` and `src/section_background.c`'s own comment),
these functions are NOT safe to call across the FULL row width — they
shift column-bytes 0-1 too, so pixel content will eventually drift into
them over repeated scrolls, silently and permanently converting that row's
colour attribute into stray pixel data. `src/section_clouds.c` doesn't use
these directly for exactly this reason — it has its own
`clouds_scroll_left()` that explicitly skips column-bytes 0-1. Only use
`hb_scroll_left_fast`/`right_fast` as-is on a canvas that doesn't reserve
any columns for attribute bytes.

## Pixel primitives

```c
void hb_set(const HiresBitmap *hb, uint8_t x, uint8_t y);
void hb_clr(const HiresBitmap *hb, uint8_t x, uint8_t y);
bool hb_get(const HiresBitmap *hb, uint8_t x, uint8_t y);
void hb_put(const HiresBitmap *hb, uint8_t x, uint8_t y, bool set);
```

Every pixel-writing primitive unconditionally sets bit6 on the resulting
byte (marks it as pixel data, not a colour attribute) and never touches
bit7 (invert) — `hires_invert_byte()` is the separate, explicit API for
that, so "set pixels" and "invert rendering" compose independently, exactly
matching how the hardware treats them as orthogonal bits.

## Lines

```c
void hbu_line(const HiresBitmap *hb, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set);
void hb_line(const HiresBitmap *hb, const HiresClip *clip, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool set);
```
Standard integer Bresenham. `hb_line`'s clip check simply skips the
`hb_put()` call for out-of-bounds points (rather than a true
Cohen-Sutherland/Liang-Barsky pre-clip) — simplest correct approach at
HIRES's small (240×200) resolution. Pass `NULL`/`0` as `clip` for
`hbu_line`'s unclipped behaviour via `hb_line` too, if convenient.

## Mode switching

```c
void hires_on(bool pal50hz);
void hires_off(void);
void hires_footer_enable(bool pal50hz);
void hires_footer_disable(void);
```

See [oric.md](oric.md)'s mode-switch attribute table for the underlying
mechanism. Two facts drive this API's design (confirmed by working through
the ULA's TEXT/HIRES addressing formulas by hand):

1. **`hires_on()`'s switch is sticky.** Once written at `TEXTVRAM+39`,
   every frame the ULA reads TEXT row 0's first raster line, hits the
   switch attribute at column 39, and re-enters HIRES for the rest of that
   same frame — no per-frame re-poking needed. This costs row 0 of the
   TEXT screen exactly one raster line's worth of visibility (a
   well-known Oric quirk).
2. **The HIRES buffer is exactly 200 scanlines + a built-in 3-row TEXT
   footer** (24 more raster lines, since TEXT rows are 8 lines tall) = 224
   total, matching the normal 28-row TEXT screen's own height (28×8=224).
   The only clean (non-glitchy) place to switch back to TEXT is the very
   last byte of the HIRES buffer (`HIRESVRAM+HIRES_SIZE-1`, i.e. `$BF3F`)
   — any earlier row lands mid-character-row and glitches that row's first
   raster line. This is also sticky: once `$BF3F` holds a TEXT-switch
   attribute, every subsequent frame's rows 200-223 (the footer) show TEXT
   content, while HIRES continues governing rows 0-199 above it — this is
   the normal way to get a persistent HUD/status-bar footer under a HIRES
   picture.

`hires_off()` is therefore **not** the same call as disabling the footer:
it clears the sticky trigger at `TEXTVRAM+39` so HIRES stops being
re-entered every frame, reverting fully to a normal 28-row TEXT screen from
the following frame onward. `hires_footer_enable()`/`hires_footer_disable()`
only affect the last HIRES byte, leaving rows 0-199 in HIRES mode.

## Attribute/colour API

```c
void hires_put_ink(uint8_t col_byte, uint8_t y, uint8_t ink);
void hires_put_paper(uint8_t col_byte, uint8_t y, uint8_t paper);
void hires_row_colors(uint8_t y, uint8_t ink, uint8_t paper);
void hires_invert_byte(const HiresBitmap *hb, uint8_t xbyte, uint8_t y, bool on);
```

Unlike the pixel primitives, `col_byte`/`xbyte` is a column-**byte** index
(0-39), not a pixel x coordinate — attributes are byte-level, no sub-byte
position. Pass an `A_FW*`/`A_BG*` constant from [oric.md](oric.md) directly
(written as given, no masking — same convention as `charwin.c`'s
`cwin_put_attr()`). `hires_row_colors()` sets INK at column-byte 0 and
PAPER at column-byte 1 of row `y`, matching `charwin.c`'s `row_setattr()`
column order.

`hires_put_ink`/`put_paper`/`row_colors` operate on the live screen
(`HIRESVRAM`) only — attributes only have meaning where the ULA is
actually scanning them out. `hires_invert_byte()` takes a `HiresBitmap*`
since the invert bit is ordinary pixel data that survives a later bitblit
from an off-screen canvas.

## AIC (Alternate Inverted Colors)

```c
typedef struct {
    uint8_t ink[2];
    uint8_t paper[2];
} HiresAIC;

void hires_aic_init(HiresAIC *aic, uint8_t ink0, uint8_t paper0, uint8_t ink1, uint8_t paper1);
void hires_aic_apply_row(const HiresAIC *aic, uint8_t y);
void hires_aic_apply_range(const HiresAIC *aic, uint8_t y0, uint8_t y1);
```

A demo-scene technique (popularised in games like *Pulsoids*) that fakes
more perceived colours than the hardware truly has: a **different**
ink/paper pair is used on even vs. odd scanlines, so eye/CRT blending of
the two alternating lines produces intermediate apparent colours.
`ink[0]`/`paper[0]` apply to even rows (`y&1==0`), `ink[1]`/`paper[1]` to
odd rows. `hires_aic_apply_range()` calls `hires_aic_apply_row()` for every
row in `[y0, y1]` inclusive.

See [ttf.md](ttf.md) for proportional text rendering, and
`tools/oric_pictconv.py --mode aic` (via `pip install -r
tools/requirements.txt`) for converting a source image into an AIC-encoded
HIRES image using this same `[parity][ink/paper]` model.

## Rect / ellipse / circle / pattern fill, flood fill

```c
void hb_rect_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool set);
void hb_rect_pattern(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t pattern[8]);
void hb_ellipse_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t rx, uint8_t ry, bool set);
void hb_circle_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t r, bool set);
void hb_flood_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, bool set);
```

All built on `hb_get`/`hb_put` (simple per-pixel loops), not hand-optimized
byte-level bit-shifting like `gfx/bitmap.c`'s heavily tuned 6502 routines —
correctness first; a future pass could special-case byte-aligned/whole-byte
spans for speed if a demo effect turns out to need it.

`hb_rect_pattern` repeats an 8×8 pixel tile (`pattern[8]`, one byte per
tile row, bit7=leftmost *of the tile*, independent of Oric's 6px HIRES byte
packing) across the rect — a monochrome hatch/texture fill, not colour
dithering (Oric has no true per-pixel colour to dither into, unlike
`gfx/mcbitmap.h`'s `MixedColors`).

`hb_ellipse_fill` grows a horizontal half-width per scanline until it
would exceed `(dx·ry)² + (dy·rx)² ≤ (rx·ry)²` (`O(rx)` per row) rather than
a Bresenham-style incremental ellipse — simple and robust at this
resolution. `hb_circle_fill` is a thin wrapper (`rx=ry=r`).

**No general polygon/triangle fill** (`hb_polygon_fill`/`hb_triangle_fill`
were removed 2026-07-15). The original implementation used an even-odd
(ray-casting) point-in-polygon test over the vertices' bounding box, which
turned out to have two real problems: (1) a documented, unresolved
Oscar64 `-O2` whole-program register-allocator bug that silently dropped
most of its fill loop's iterations at *some* call sites, resistant to
every previously-tried mitigation (see `~/.claude/oscar64.md`'s "Third
symptom shape" entry); (2) even when it did run correctly, a division
inside the innermost per-pixel loop made it take several real *seconds*
to fill an ordinary-sized shape on this 1MHz CPU — confirmed via
Phosphoric frame-dumps showing a star visibly growing on screen over that
whole time, in two separate sections that both used it. Every real call
site in this project has since moved to the pattern below, which has
neither problem:

**Recommended pattern for a filled arbitrary shape**: draw the shape's
own closed outline via repeated `hb_line()` calls (one per edge, wrapping
back to the first vertex), then `hb_flood_fill()` from a point known to
be inside it (e.g. a symmetric shape's own centre/centroid). See
`src/section_hires_showcase.c`'s `SHOW_STAR` state or
`src/section_rasterirq_showcase.c`'s `draw_star()` for two worked
examples (both draw a 10-vertex star this way). The one real tradeoff:
the caller must supply a valid interior seed point themselves — not
automatic for an arbitrary/non-convex shape the way the old point-in-
polygon test was — but `hb_flood_fill()` itself has no per-pixel division
at all (see below), so this is dramatically faster and carries none of
the miscompilation history above.

`hb_flood_fill` (paint bucket) is a non-recursive scanline-stack algorithm
adapted from OSDK's
["Flood Fill"](https://osdk.org/index.php?page=articles&ref=ART18) article:
fill a contiguous horizontal span in one pass, then queue at most **one**
seed per contiguous matching run on the rows immediately above/below (not
one seed per pixel) — this is what keeps the internal seed stack
(`HB_FLOOD_STACK_SIZE`, 128 entries) shallow in practice, deliberately
avoiding the 6502's 256-byte hardware stack via recursion (matching this
codebase's existing "large arrays must be `static`" discipline, e.g.
`charwin.h`'s `OricViewport`). If the stack fills up, the flood simply
stops spreading past that point — a partial result, not a crash.

## Bitblit

```c
typedef enum {
    HBLIT_COPY,   // destination = source
    HBLIT_OR,     // destination |= source
    HBLIT_AND,    // destination &= source
    HBLIT_XOR     // destination ^= source
} HiresBlitOp;

void hb_bitblit(const HiresBitmap *dst, const HiresClip *clip, uint8_t dx, uint8_t dy,
                 const HiresBitmap *src, uint8_t sx, uint8_t sy, uint8_t w, uint8_t h, HiresBlitOp op);
```

Copies (or combines) a `w`×`h` pixel region from a source canvas to a
destination canvas, per-pixel via `hb_get`/`hb_put`. Unlike `gfx/bitmap.c`'s
`BlitOp`, there is no pattern-fill variant — Oric has no true per-pixel
colour to dither a pattern into. Note: `hb_bitblit` copies **pixel bits
only** (bits 0-5), not bit7 (invert) — a destination byte's existing
invert state is left as-is; use `hires_invert_byte()` separately if the
copied region needs it.

**Not safe for overlapping same-canvas source/destination regions** — no
reverse-iteration handling like `gfx/bitmap.c`'s `BlitOp`/`reverse` flag.
Don't `hb_bitblit` a canvas onto itself (e.g. to scroll it); see "Canvas
scroll" above for dedicated, overlap-safe functions instead. Used for
"save-under" sprites in [sprite.md](sprite.md).

## Text (ROM charset)

```c
int hb_put_chars(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, const char *str, uint8_t len);
int hb_put_chars_left(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);
int hb_put_chars_center(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);
int hb_put_chars_right(const HiresBitmap *hb, const HiresClip *clip, uint8_t y, const char *str, uint8_t len);
```

Renders using the Oric ROM's standard 6×8 charset (`CHARSETROM`, see
[oric.md](oric.md)/[charset.md](charset.md)), printable ASCII `0x20-0x7F`
only. A ROM glyph byte's bits5-0 are already left-aligned exactly as HIRES
pixel bytes expect (bit5 = leftmost), so no reformatting is needed —
`hb_put_chars` just walks each glyph row's bits through `hb_put`. Returns
the pixel width consumed (`drawn_chars * 6`). Fixed-width only; for a
proportional font, see [ttf.md](ttf.md).

`hb_put_chars_left/center/right` mirror `ttf_print_left/center/right`
([ttf.md](ttf.md)), using `hb_put_chars`'s fixed `len*6` pixel width
instead of `ttf_strlen()` since the ROM charset isn't proportional.

### Advanced: up to 4 charsets via mid-frame TEXT/HIRES switching

No new API needed for this — it falls out of what `hires_on()`/
`hires_off()`/`hires_footer_enable()` already do. Per OSDK's
["Charsets"](https://osdk.org/index.php?page=articles&ref=ART8) article:
since TEXT mode's charset banks (`CHARSET_STD`/`CHARSET_ALT`, `$B400`/
`$B800`) and HIRES mode's (`HIRES_CHARSET_STD`/`ALT`, `$9800`/`$9C00`) are
at *different* addresses, alternating TEXT/HIRES mode more than once
within a single displayed frame gives access to up to 4 distinct charset
banks in that one frame — e.g. a HIRES picture with a TEXT-mode header
*and* footer strip (each switch costing its own sticky mode-switch byte,
same mechanism as the footer), each able to use different charset content
than the main HIRES area's own footer.
