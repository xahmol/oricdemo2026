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
