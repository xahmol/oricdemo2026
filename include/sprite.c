// sprite.c - see sprite.h.

#include "sprite.h"

void hspr_init(HiresSprite *spr, uint8_t *image_data, uint8_t *backup_data, uint8_t w, uint8_t h)
{
    hb_init(&spr->image, image_data, h);
    hb_init(&spr->backup, backup_data, h);
    spr->w = w;
    spr->h = h;
    spr->x = 0;
    spr->y = 0;
    spr->visible = false;
}

void hspr_draw(const HiresBitmap *screen, HiresSprite *spr, uint8_t x, uint8_t y)
{
    hb_bitblit(&spr->backup, (const HiresClip *)0, 0, 0, screen, x, y, spr->w, spr->h, HBLIT_COPY);
    hb_bitblit(screen, (const HiresClip *)0, x, y, &spr->image, 0, 0, spr->w, spr->h, HBLIT_COPY);
    spr->x = x;
    spr->y = y;
    spr->visible = true;
}

void hspr_erase(const HiresBitmap *screen, HiresSprite *spr)
{
    if (!spr->visible)
        return;
    hb_bitblit(screen, (const HiresClip *)0, spr->x, spr->y, &spr->backup, 0, 0, spr->w, spr->h, HBLIT_COPY);
    spr->visible = false;
}

void hxspr_draw(const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                 uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color,
                 uint8_t *color_backup)
{
    uint8_t *dst = screen->data + (uint16_t)y * HIRES_ROW_BYTES + col;
    uint8_t row;

    if (color && color->enabled)
    {
        uint8_t *left = dst - 1;
        uint8_t *right = dst + w_bytes;
        for (row = 0; row < h; row++)
        {
            if (color_backup)
            {
                color_backup[0] = *left;
                color_backup[1] = *right;
                color_backup += 2;
            }
            *left = color->ink;
            *right = color->restore_ink;
            left += HIRES_ROW_BYTES;
            right += HIRES_ROW_BYTES;
        }
    }

    for (row = 0; row < h; row++)
    {
        uint8_t i;
        if (mode == HXSPR_OR || mode == HXSPR_OR_SPARSE)
        {
            for (i = 0; i < w_bytes; i++)
            {
                if (mode == HXSPR_OR_SPARSE && image[i] == 0xFF)
                    continue;   // "hole" column -- leave dst/backup completely untouched
                backup[i] = dst[i];
                dst[i] = (uint8_t)(dst[i] | (image[i] & 0x3F) | 0x40);
            }
            backup += w_bytes;
        }
        else   // HXSPR_XOR
        {
            for (i = 0; i < w_bytes; i++)
                dst[i] = (uint8_t)((dst[i] ^ (image[i] & 0x3F)) | 0x40);
        }
        dst += HIRES_ROW_BYTES;
        image += w_bytes;
    }
}

void hxspr_erase(const HiresBitmap *screen, const uint8_t *image, uint8_t w_bytes, uint8_t h,
                  uint8_t col, uint8_t y, HxsprMode mode, uint8_t *backup, const HxsprColor *color,
                  uint8_t *color_backup)
{
    uint8_t *dst = screen->data + (uint16_t)y * HIRES_ROW_BYTES + col;
    uint8_t row;

    for (row = 0; row < h; row++)
    {
        uint8_t i;
        if (mode == HXSPR_OR || mode == HXSPR_OR_SPARSE)
        {
            for (i = 0; i < w_bytes; i++)
            {
                if (mode == HXSPR_OR_SPARSE && image[i] == 0xFF)
                    continue;   // matches the draw call's own skip -- see hxspr_draw()
                dst[i] = backup[i];
            }
            backup += w_bytes;
        }
        else   // HXSPR_XOR
        {
            for (i = 0; i < w_bytes; i++)
                dst[i] = (uint8_t)((dst[i] ^ (image[i] & 0x3F)) | 0x40);
        }
        dst += HIRES_ROW_BYTES;
        image += w_bytes;
    }

    if (color && color->enabled)
    {
        // Revert both bracket columns to their real saved bytes
        // (color_backup, filled in by the matching hxspr_draw() call) --
        // NOT a hardcoded blank, which would silently destroy any real
        // pixel/attribute content that happened to be sitting there (see
        // HxsprColor's own comment). Falls back to hardcoded blank pixel
        // data (0x40) only if the caller didn't provide a color_backup
        // buffer, matching this function's old behaviour for that case.
        uint8_t *left = screen->data + (uint16_t)y * HIRES_ROW_BYTES + col - 1;
        uint8_t *right = screen->data + (uint16_t)y * HIRES_ROW_BYTES + col + w_bytes;
        for (row = 0; row < h; row++)
        {
            if (color_backup)
            {
                *left = color_backup[0];
                *right = color_backup[1];
                color_backup += 2;
            }
            else
            {
                *left = 0x40;
                *right = 0x40;
            }
            left += HIRES_ROW_BYTES;
            right += HIRES_ROW_BYTES;
        }
    }
}
