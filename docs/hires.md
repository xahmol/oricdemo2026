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

## Rect / circle / triangle / polygon fill

```c
void hb_rect_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool set);
void hb_circle_fill(const HiresBitmap *hb, const HiresClip *clip, uint8_t cx, uint8_t cy, uint8_t r, bool set);
void hb_polygon_fill(const HiresBitmap *hb, const HiresClip *clip, const uint8_t *xs, const uint8_t *ys, uint8_t num, bool set);
void hb_triangle_fill(const HiresBitmap *hb, const HiresClip *clip,
                       uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, bool set);
```

All built on `hb_get`/`hb_put` (simple per-pixel loops), not hand-optimized
byte-level bit-shifting like `gfx/bitmap.c`'s heavily tuned 6502 routines —
correctness first; a future pass could special-case byte-aligned/whole-byte
spans for speed if a demo effect turns out to need it.

`hb_circle_fill` grows a horizontal half-width per scanline until it would
exceed the radius (`O(r)` per row) rather than a Bresenham-style
incremental circle — simple and robust at this resolution.

`hb_polygon_fill` uses an even-odd (ray-casting) point-in-polygon test over
the vertices' bounding box — handles convex **and** simple concave
polygons with one algorithm, unlike `gfx/bitmap.c`'s separate
convex-only/non-convex routines. `hb_triangle_fill` is a thin convenience
wrapper over it.

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

## Text (ROM charset)

```c
int hb_put_chars(const HiresBitmap *hb, const HiresClip *clip, uint8_t x, uint8_t y, const char *str, uint8_t len);
```

Renders using the Oric ROM's standard 6×8 charset (`CHARSETROM`, see
[oric.md](oric.md)/[charset.md](charset.md)), printable ASCII `0x20-0x7F`
only. A ROM glyph byte's bits5-0 are already left-aligned exactly as HIRES
pixel bytes expect (bit5 = leftmost), so no reformatting is needed —
`hb_put_chars` just walks each glyph row's bits through `hb_put`. Returns
the pixel width consumed (`drawn_chars * 6`). Fixed-width only; for a
proportional font, see [ttf.md](ttf.md).
