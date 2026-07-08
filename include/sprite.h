// sprite.h - "Save-under" sprite system for HIRES mode, built entirely on
// hires.h's hb_bitblit(HBLIT_COPY). The Oric has no hardware sprites; this
// is the standard software technique: back up the screen region a sprite
// is about to cover before drawing it, then blit that backup back to erase.
//
// Note on memory cost: HiresBitmap (hires.h) canvases are always 40 bytes/
// row (full scanline width) regardless of how few columns are actually
// used -- there is no "narrow canvas" concept. A HiresSprite's image/backup
// buffers therefore cost `h * 40` bytes EACH (80 bytes/row total for the
// pair), even for a visually narrow sprite -- only `w` (<=HIRES_WIDTH_PX)
// controls how many of those 40 columns hb_bitblit actually copies per
// row. Budget accordingly, especially under the HIRES runtime's already
//-reduced RAM (see oric_crt_hires.c).

#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>
#include <stdbool.h>
#include "hires.h"

typedef struct {
    HiresBitmap image;    // sprite pixel data (off-screen canvas)
    HiresBitmap backup;   // scratch canvas, same size as image -- holds saved background while visible
    uint8_t w, h;          // sprite's visible width/height in pixels (w <= HIRES_WIDTH_PX)
    uint8_t x, y;          // current on-screen position (meaningful only while visible)
    bool visible;
} HiresSprite;

// Wires up spr->image/backup over caller-owned image_data/backup_data
// (each must be at least h*HIRES_ROW_BYTES bytes -- this module never
// allocates). Caller fills spr->image's pixel data afterward (e.g. via
// hb_* drawing calls or hb_bitblit from a converted asset) before the
// first hspr_draw().
void hspr_init(HiresSprite *spr, uint8_t *image_data, uint8_t *backup_data, uint8_t w, uint8_t h);

// Backs up the screen region about to be overdrawn into spr->backup, then
// blits spr->image over it at (x,y). Safe to call again while already
// visible (e.g. to move the sprite) -- erases the OLD position's backup
// implicitly by simply overwriting spr->backup with the NEW position's
// content; call hspr_erase() first if you need the old position restored
// before moving.
void hspr_draw(const HiresBitmap *screen, HiresSprite *spr, uint8_t x, uint8_t y);

// Blits spr->backup back over the sprite's current position, restoring
// the screen content it was covering, and marks the sprite not visible.
// No-op if the sprite isn't currently visible.
void hspr_erase(const HiresBitmap *screen, HiresSprite *spr);

// -------------------------------------------------------------------------
// Fast byte-aligned sprite -- a much faster alternative to HiresSprite for
// sprites that only ever move in whole column-bytes (6px steps). HiresSprite's
// hb_bitblit-based save-under system costs a per-pixel hb_get/hb_put call
// for every pixel of every blit (three full blits per move: backup-save,
// image-draw, backup-restore) -- fine for a small sprite, far too slow at
// 1MHz for anything sprite-sized-and-up (e.g. a 66x64 walking character).
//
// hxspr_draw()/hxspr_erase() instead operate on a tightly-packed,
// byte-aligned image directly against the screen: no per-pixel addressing.
// Stateless (no init/struct) -- the caller owns the image data and tracks
// its own current column/row/frame, exactly like HiresSprite's
// image_data/backup_data buffers are caller-owned. `mode` picks the
// compositing technique:
//
//   HXSPR_XOR -- self-inverting, no backup buffer needed (pass NULL).
//     Only bits 0-5 (pixel data) of each byte are XORed; bit6 is always
//     forced set afterward and bit7 (invert) is never touched -- same
//     convention as hb_set/hb_put. hxspr_erase() undoes a HXSPR_XOR sprite
//     by re-XORing the SAME image at the SAME col/y -- you must pass the
//     currently-drawn image/position, not the new one.
//   HXSPR_OR -- additive (dst |= src), needs a caller-owned backup buffer
//     (w_bytes*h bytes). hxspr_draw() saves the screen's original bytes
//     into `backup` before OR-merging; hxspr_erase() copies them straight
//     back. Restores byte-exact regardless of what's underneath -- more
//     robust than XOR (no "must match the last draw exactly" constraint),
//     at the cost of that extra buffer.
//
// Neither mode is truly opaque: both only touch screen bits where the
// sprite's own bit is 1 (XOR toggles them, OR sets them) -- wherever the
// sprite is "off", the background shows straight through untouched. See
// docs/sprite.md for the full writeup and tradeoffs vs. HiresSprite.
// -------------------------------------------------------------------------

typedef enum {
    HXSPR_XOR,
    HXSPR_OR
} HxsprMode;

// Optional ink-only colour bracket (see docs/sprite.md's "Colour" section).
// If enabled, hxspr_draw() pokes a single INK attribute byte at column
// `col-1` (the sprite's own colour) and another at `col+w_bytes`
// (restoring the baseline colour for the rest of the scanline) on every
// row the sprite occupies -- attribute bytes are per-scanline, so this
// repeats every row, not just once. hxspr_erase() reverts BOTH bracket
// columns to ordinary blank pixel data (0x40), NOT to an ink attribute --
// a moving sprite visits many different columns over its lifetime, and
// leaving a past position's bracket as a permanent attribute byte would
// steal that column from the background bitmap forever (it can never show
// pixel content again). This assumes the background under the brackets is
// blank; a sprite moving over real background art at the bracket columns
// would need actual save/restore of those 2 bytes instead of a hardcoded
// 0x40, not built here. PAPER is assumed constant across the whole screen
// (set once, e.g. via hires_row_colors()) and is never touched here -- if
// a sprite ever needs its own PAPER too, that's a bigger change (see
// docs/sprite.md). Caller must leave column `col-1` free (col >= 1) and
// `col+w_bytes` within the row (col+w_bytes <= HIRES_ROW_BYTES-1).
typedef struct {
    bool    enabled;
    uint8_t ink;           // this sprite's own ink, set at col-1 while drawn
    uint8_t restore_ink;   // baseline ink, set at col+w_bytes while drawn (erase reverts both brackets to blank pixel data instead)
} HxsprColor;

// Draws `image` (w_bytes wide, h rows, tightly packed -- w_bytes*h bytes
// total, NOT the h*HIRES_ROW_BYTES a HiresSprite canvas costs) onto
// `screen` at column-byte `col`, row `y`, per `mode`. Pass `color` as NULL
// (or `enabled=false`) for a plain monochrome sprite.
void hxspr_draw(const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                 uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color);

// Restores the screen at `col`/`y`. For HXSPR_XOR, `image` must be the
// SAME image currently drawn there (re-XORs it away); for HXSPR_OR,
// `image` is ignored (restores from `backup` instead). If `color` was
// used to draw, pass the same `color` here too, to revert both bracket
// columns to blank pixel data (see HxsprColor's own comment).
void hxspr_erase(const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                  uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color);

#pragma compile("sprite.c")

#endif // SPRITE_H
