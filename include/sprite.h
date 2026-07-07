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

#pragma compile("sprite.c")

#endif // SPRITE_H
