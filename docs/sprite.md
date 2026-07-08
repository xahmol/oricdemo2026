# Sprite system (sprite.h)

"Save-under" sprites for HIRES mode, built entirely on
[hires.h](hires.md)'s `hb_bitblit(HBLIT_COPY)`. The Oric has no hardware
sprites; this is the standard software technique: back up the screen
region a sprite is about to cover before drawing it, then blit that
backup back to erase. Include `sprite.h`; it auto-compiles `sprite.c` via
`#pragma compile`.

```c
typedef struct {
    HiresBitmap image;    // sprite pixel data (off-screen canvas)
    HiresBitmap backup;   // scratch canvas, same size as image -- holds saved background
    uint8_t w, h;          // sprite's visible width/height in pixels (w <= HIRES_WIDTH_PX)
    uint8_t x, y;          // current on-screen position (meaningful only while visible)
    bool visible;
} HiresSprite;

void hspr_init(HiresSprite *spr, uint8_t *image_data, uint8_t *backup_data, uint8_t w, uint8_t h);
void hspr_draw(const HiresBitmap *screen, HiresSprite *spr, uint8_t x, uint8_t y);
void hspr_erase(const HiresBitmap *screen, HiresSprite *spr);
```

`hspr_init()` wires `spr->image`/`spr->backup` over caller-owned
`image_data`/`backup_data` buffers (each must be at least
`h * HIRES_ROW_BYTES` bytes — this module never allocates). Fill
`spr->image`'s pixel data afterward (`hb_*` drawing calls, or
`hb_bitblit` from a converted asset) before the first `hspr_draw()`.

`hspr_draw(screen, spr, x, y)` backs up the screen region about to be
overdrawn into `spr->backup`, then blits `spr->image` over it at
`(x, y)`. Safe to call again while already visible (e.g. to move the
sprite) — this simply overwrites `spr->backup` with the *new* position's
content, so call `hspr_erase()` first if you need the *old* position
restored before moving.

`hspr_erase(screen, spr)` blits `spr->backup` back over the sprite's
current position, restoring the screen content it was covering, and marks
the sprite not visible. No-op if the sprite isn't currently visible.

## Memory cost

[hires.h](hires.md)'s `HiresBitmap` canvases are always 40 bytes/row (full
scanline width) regardless of how few columns are actually used — there
is no "narrow canvas" concept. A `HiresSprite`'s `image`/`backup` buffers
therefore cost `h * 40` bytes **each** (80 bytes/row total for the pair),
even for a visually narrow sprite — only `w` (`<= HIRES_WIDTH_PX`)
controls how many of those 40 columns `hb_bitblit` actually copies per
row. Budget accordingly, especially under the HIRES runtime's already-
reduced RAM (see [hires.md](hires.md)'s memory-layout section).

## No mask plane (yet)

This is an opaque-rectangle sprite: `hspr_draw` always overwrites the full
`w`×`h` rect. A future mask-based variant (irregular/non-rectangular
shapes via `HBLIT_AND`/`HBLIT_OR`) is a possible follow-up, not built here.

## Fast byte-aligned sprite (`hxspr_draw`/`hxspr_erase`) -- the fast alternative

`HiresSprite` above costs a per-pixel `hb_get`/`hb_put` call for every pixel
of every blit (three full blits per move: backup-save, image-draw,
backup-restore) — fine for something small, but far too slow at 1MHz for a
sprite of any real size (e.g. `src/section_bird.c`'s 66×64 walking bird was
originally built on `HiresSprite` and was unusably slow — see git history).

```c
typedef enum { HXSPR_XOR, HXSPR_OR } HxsprMode;

typedef struct {
    bool    enabled;
    uint8_t ink;           // this sprite's own ink, set at col-1
    uint8_t restore_ink;   // baseline ink, restored at col+w_bytes (always) and col-1 on erase
} HxsprColor;

void hxspr_draw (const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                  uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color);
void hxspr_erase(const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                  uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color);
```

Operates on a tightly-packed image (`w_bytes * h` bytes, one byte per 6
pixels — **not** the `h * HIRES_ROW_BYTES` a `HiresSprite` canvas costs)
directly against `screen` at column-byte `col`, row `y`. Only bits 0-5
(pixel data) of each byte are touched; bit6 is always forced set afterward
and bit7 (invert) is never touched, matching `hb_set`/`hb_put`'s own
convention. **Stateless, no init/struct** — the caller owns the image data
and tracks its own current column/row/frame (like `HiresSprite`'s buffers
are caller-owned, just without any bookkeeping struct at all).

### Mode: `HXSPR_XOR` vs. `HXSPR_OR`

**`HXSPR_XOR`** — self-inverting, `backup` unused (pass `NULL`). XOR is its
own inverse: `hxspr_erase()` re-XORs the exact same `image`/`col`/`y` that
was last drawn, which restores the screen exactly. Usage pattern for an
animated, moving sprite:

```c
hxspr_draw (&screen, frame_ptr, w_bytes, h, col, y, HXSPR_XOR, NULL, NULL);   // draw
// ... later, before moving or switching frame:
hxspr_erase(&screen, frame_ptr, w_bytes, h, col, y, HXSPR_XOR, NULL, NULL);   // undo -- SAME args
col = new_col; frame_ptr = next_frame_ptr;
hxspr_draw (&screen, frame_ptr, w_bytes, h, col, y, HXSPR_XOR, NULL, NULL);   // draw new frame/position
```

XOR only looks correct if you always undo the exact same image/position
before drawing a different one — skipping the undo step causes visible
ghosting, since two different XORs at the same spot don't cancel out.

**`HXSPR_OR`** — additive (`dst |= src`), needs a caller-owned `backup`
buffer (`w_bytes * h` bytes). `hxspr_draw()` saves the screen's original
bytes into `backup` *before* OR-merging; `hxspr_erase()` copies them
straight back (`image` is ignored on erase for this mode). This restores
byte-exact regardless of what's underneath — more robust than XOR (no
"must match the last draw exactly" constraint), at the cost of that extra
buffer (same size as the image).

Neither mode is truly opaque: both only touch screen bits where the
sprite's own bit is 1 — wherever the sprite is "off", the background shows
straight through untouched. A genuinely opaque fast sprite (full rect
replace, like `HiresSprite` but byte-aligned) isn't built here — would need
a third mode, not built because nothing has needed it yet.

### Colour (`HxsprColor`)

HIRES colour isn't per-pixel — it's a byte-level INK/PAPER attribute that
applies to **the rest of the scanline** from that column onward until
another attribute byte changes it (see [oric.md](oric.md)'s mode-switch
table). Giving a sprite its own ink therefore risks bleeding that colour
into everything to its right on the same scanline, unless bracketed.

Pass a `const HxsprColor *color` (or `NULL`/`enabled=false` for a plain
monochrome sprite) to reserve **one column-byte immediately before** `col`
(`col-1`, set to `color->ink` — the sprite's own colour) and **one
immediately after** the image (`col+w_bytes`, set to `color->restore_ink`
— the baseline colour for the rest of the row), on every row the sprite
occupies while drawn (attribute bytes are per-scanline, so this repeats
every row, not just once).

`hxspr_erase()` reverts **both** bracket columns to ordinary blank pixel
data (`0x40`), not to an ink attribute — this matters for a *moving*
sprite: it visits many different columns over its lifetime, and leaving a
past position's bracket as a permanent attribute byte would steal that
column from the background bitmap forever (an attribute byte never shows
pixel content again, no matter what's drawn there later). This assumes the
background under the bracket columns is blank — true for this demo's
current blank canvas. A sprite moving colour-bracketed over *real*
background art at those exact columns would need actual save/restore of
those 2 bytes instead of a hardcoded blank, which isn't built here (not
needed yet). This was a real bug caught during development: the first
version restored to `restore_ink` (an attribute byte) instead of blank
pixel data, which — for a sprite moving across ~28 different column
positions — permanently converted a growing swath of the row into
stuck attribute bytes, visible as spurious vertical colour banding.

**This only handles INK, not PAPER** — PAPER is assumed constant across the
whole screen (set once, e.g. via a `hires_row_colors()` sweep over every
row at startup — see `src/main.c`) and is never touched by `HxsprColor`. A
sprite that also needs its own PAPER (not just ink) would need a bigger
change (2 more bracket bytes, one each side, for the paper attribute too)
— not built here because nothing has needed it yet.

**Cost**: 2 extra column-bytes total (`col-1` and `col+w_bytes`), on top of
the image's own `w_bytes` — the caller must leave `col >= 1` and
`col+w_bytes <= HIRES_ROW_BYTES-1` room for them (see
`src/section_bird.c`'s `BIRD_MIN_COL`/`BIRD_MAX_COL` for a worked example:
an 11-byte image reserves a 13-byte total footprint, shrinking its
column-byte travel range from 30 positions to 28).

**Why this is cheap in practice**: if the *whole picture* uses one fixed,
known ink/paper per row (the `main.c` convention above), `restore_ink` is
just that same constant — no per-position backup/lookup needed. If
background colour genuinely varies within a row a sprite crosses, the
`col-1`/`col+w_bytes` bytes would need their own tiny save/restore (same
technique as `HXSPR_OR`'s backup, just for 1 byte) — not built here, since
the current demo doesn't need it.

**Ceiling**: a sprite gets one ink/paper pair for its whole body per
scanline (can differ row-to-row at no extra cost, since each row's bracket
is independent) — not multiple simultaneous colours across its own width
without further splitting and further bracket bytes. `HiresAIC` (hires.h)
could layer in an even/odd-row dither look at double the bracket cost, if
wanted later — not built here.

### Tradeoffs vs. `HiresSprite`

- Movement is constrained to whole column-bytes (6px steps) — no sub-byte
  x positioning, since there's no per-pixel compositing to fall back on.
- The sprite's image data must be pre-packed tightly (`w_bytes` bytes/row),
  not a `HiresBitmap` — see `src/section_bird.c`/`assets/bird.h` for a
  worked example (multi-frame animation: each frame is `w_bytes * h` bytes,
  addressed via pointer arithmetic into one flat array).
- No `HBLIT_AND` equivalent — XOR or OR only.

For a small sprite, or one that genuinely needs arbitrary sub-pixel
positioning, `HiresSprite` is still the right tool. For anything
sprite-sized-and-up that only needs to move in 6px steps, prefer
`hxspr_draw`/`hxspr_erase`.
