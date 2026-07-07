// ttf.c - see ttf.h for provenance and API rationale.

#include <stdbool.h>
#include "ttf.h"

uint8_t ttf_space = 1;

uint16_t ttf_strlen(const TtfFont *font, const char *s)
{
    uint16_t total = 0;
    bool first = true;
    for (const char *p = s; *p; p++)
    {
        uint8_t code = (uint8_t)*p;
        if (code < font->first_char || code > font->last_char)
            continue;
        if (!first)
            total = (uint16_t)(total + ttf_space);
        total = (uint16_t)(total + font->widths[code - font->first_char]);
        first = false;
    }
    return total;
}

uint16_t ttf_print(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t x, uint8_t y, const char *s)
{
    uint8_t startx = x;
    bool first = true;

    for (const char *p = s; *p; p++)
    {
        uint8_t code = (uint8_t)*p;
        if (code < font->first_char || code > font->last_char)
            continue;

        if (!first)
            x = (uint8_t)(x + ttf_space);
        first = false;

        uint8_t idx = (uint8_t)(code - font->first_char);
        uint8_t width = font->widths[idx];
        uint8_t wbytes = font->width_bytes[idx];
        const uint8_t *glyph = font->char_defs + font->offsets[idx];

        for (uint8_t row = 0; row < font->h; row++)
        {
            uint8_t py = (uint8_t)(y + row);
            if (clip && (py < clip->top || py > clip->bottom))
                continue;
            const uint8_t *rowptr = glyph + (uint16_t)row * wbytes;
            for (uint8_t col = 0; col < width; col++)
            {
                uint8_t px = (uint8_t)(x + col);
                if (clip && (px < clip->left || px > clip->right))
                    continue;
                uint8_t byte = rowptr[col / 6];
                bool on = (byte & (0x20 >> (col % 6))) != 0;
                hb_put(hb, px, py, on);
            }
        }

        x = (uint8_t)(x + width);
    }

    return (uint16_t)(x - startx);
}

uint16_t ttf_print_left(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s)
{
    return ttf_print(hb, clip, font, 0, y, s);
}

uint16_t ttf_print_center(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s)
{
    uint16_t w = ttf_strlen(font, s);
    uint8_t x = (w >= HIRES_WIDTH_PX) ? 0 : (uint8_t)((HIRES_WIDTH_PX - w) / 2);
    return ttf_print(hb, clip, font, x, y, s);
}

uint16_t ttf_print_right(const HiresBitmap *hb, const HiresClip *clip, const TtfFont *font, uint8_t y, const char *s)
{
    uint16_t w = ttf_strlen(font, s);
    uint8_t x = (w >= HIRES_WIDTH_PX) ? 0 : (uint8_t)(HIRES_WIDTH_PX - w);
    return ttf_print(hb, clip, font, x, y, s);
}
