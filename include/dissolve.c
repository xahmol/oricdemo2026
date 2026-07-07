// dissolve.c - see dissolve.h.

#include "dissolve.h"

// 240*200=48000 overflows a 16-bit signed int (INT16_MAX=32767) if
// multiplied directly -- force the multiplication into 32 bits first,
// then truncate (the result itself fits comfortably in uint16_t).
#define _HIRES_PIXEL_COUNT ((uint16_t)((uint32_t)HIRES_WIDTH_PX * HIRES_ROWS))

void hires_row_colors_range(uint8_t y0, uint8_t y1, uint8_t stride, uint8_t ink, uint8_t paper)
{
    for (uint8_t y = y0; y <= y1; y = (uint8_t)(y + stride))
        hires_row_colors(y, ink, paper);
}

static uint16_t _dissolve_lfsr;

void hires_dissolve_init(uint16_t seed)
{
    _dissolve_lfsr = seed ? seed : 1;
}

uint16_t hires_dissolve_next(void)
{
    uint16_t val;
    do
    {
        uint16_t lsb = (uint16_t)(_dissolve_lfsr & 1);
        _dissolve_lfsr = (uint16_t)(_dissolve_lfsr >> 1);
        if (lsb)
            _dissolve_lfsr = (uint16_t)(_dissolve_lfsr ^ 0xB400);
        val = _dissolve_lfsr;
    } while (val >= _HIRES_PIXEL_COUNT);
    return val;
}

void hires_dissolve_step(const HiresBitmap *hb, uint16_t position, bool set)
{
    uint8_t y = (uint8_t)(position / HIRES_WIDTH_PX);
    uint8_t x = (uint8_t)(position % HIRES_WIDTH_PX);
    hb_put(hb, x, y, set);
}
