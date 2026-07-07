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
