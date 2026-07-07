// charset.c - Generic Oric charset-bank addressing and copy primitives
//
// See charset.h for the design rationale.

#include <string.h>
#include "oric.h"
#include "charset.h"

/**
 * Compute the charset-RAM address of a glyph's 8-byte bitmap in the
 * CHARSET_STD or CHARSET_ALT bank. Full 128-glyph addressing
 * (base + screencode*8, no -0x20 offset) -- see oric.h for the
 * CHARSETROM convention, which differs.
 *
 * @param screencode Screencode (0x00-0x7F) whose glyph address is wanted.
 * @param altorstd   0 for CHARSET_STD, non-zero for CHARSET_ALT.
 * @return Absolute address of the glyph's 8-byte bitmap in charset RAM.
 */
uint16_t charset_address(uint8_t screencode, uint8_t altorstd)
{
    uint16_t base = (altorstd == 0) ? CHARSET_STD : CHARSET_ALT;
    return base + (uint16_t)screencode * 8;
}

/**
 * Displayable-range byte count for charset bank `base` -- 640
 * (CHARSET_ALT_GLYPH_AREA_SIZE) for CHARSET_ALT (the only 896 bytes of
 * real RAM it has before screen RAM begins at $BB80), 768
 * (CHARSET_GLYPH_AREA_SIZE) otherwise.
 *
 * @param base CHARSET_STD or CHARSET_ALT base address.
 * @return The byte count to use for that bank's displayable range.
 */
uint16_t charset_area_size(uint16_t base)
{
    return (base == CHARSET_ALT) ? CHARSET_ALT_GLYPH_AREA_SIZE : CHARSET_GLYPH_AREA_SIZE;
}

/**
 * Copy the displayable glyph range (codes 0x20-0x7F, charset_area_size()
 * bytes) of a charset bank into a RAM buffer.
 *
 * @param base CHARSET_STD or CHARSET_ALT base address.
 * @param dest Destination buffer, at least charset_area_size(base) bytes.
 * @return (none)
 */
void charset_save(uint16_t base, uint8_t *dest)
{
    memcpy(dest, (uint8_t *)(base + CHARSET_GLYPH_AREA_OFFSET), charset_area_size(base));
}

/**
 * Copy a buffer (as produced by charset_save()) back into the displayable
 * glyph range (codes 0x20-0x7F, charset_area_size() bytes) of a charset
 * bank.
 *
 * @param base CHARSET_STD or CHARSET_ALT base address.
 * @param src  Source buffer, at least charset_area_size(base) bytes.
 * @return (none)
 */
void charset_load(uint16_t base, const uint8_t *src)
{
    memcpy((uint8_t *)(base + CHARSET_GLYPH_AREA_OFFSET), src, charset_area_size(base));
}

/**
 * Locate a glyph's 8-byte bitmap in the ROM-resident standard charset
 * table (CHARSETROM). Uses the (screencode-0x20)*8 convention -- the ROM
 * table only covers the 96 printable codes 0x20-0x7F, unlike
 * CHARSET_STD/CHARSET_ALT's full 128-glyph banks (see charset_address()).
 *
 * @param screencode Screencode (0x20-0x7F) whose ROM glyph is wanted.
 * @return Pointer to the glyph's 8-byte bitmap in CHARSETROM.
 */
const uint8_t *charset_rom_glyph(uint8_t screencode)
{
    return (const uint8_t *)(CHARSETROM + (uint16_t)(screencode - 0x20) * 8);
}

// Bitmask covering the CHARSET_GLYPH_WIDTH visible bits of a glyph row.
#define CHARSET_GLYPH_MASK   ((1 << CHARSET_GLYPH_WIDTH) - 1)
// Leftmost-bit (bit5) value.
#define CHARSET_GLYPH_MSB    (1 << (CHARSET_GLYPH_WIDTH - 1))

/**
 * Invert every pixel of a glyph in place (XOR each row with the
 * CHARSET_GLYPH_WIDTH-bit mask).
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_invert(volatile uint8_t *glyph)
{
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++) glyph[y] ^= CHARSET_GLYPH_MASK;
}

/**
 * Mirror a glyph vertically in place (reverse the order of its rows).
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_mirror_v(volatile uint8_t *glyph)
{
    uint8_t buf[CHARSET_GLYPH_BYTES];
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++) buf[y] = glyph[CHARSET_GLYPH_BYTES - 1 - y];
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++) glyph[y] = buf[y];
}

/**
 * Mirror a glyph horizontally in place (reverse the bit order of each row
 * within the CHARSET_GLYPH_WIDTH-bit pixel field).
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_mirror_h(volatile uint8_t *glyph)
{
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++)
    {
        uint8_t present = glyph[y];
        uint8_t v = 0;
        uint8_t bx;
        for (bx = 0; bx < CHARSET_GLYPH_WIDTH; bx++)
            if (present & (1 << (CHARSET_GLYPH_WIDTH - 1 - bx))) v |= (uint8_t)(1 << bx);
        glyph[y] = v;
    }
}

/**
 * Scroll a glyph's rows up by one pixel in place, wrapping the top row to
 * the bottom.
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_scroll_up(volatile uint8_t *glyph)
{
    uint8_t first = glyph[0];
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES - 1; y++) glyph[y] = glyph[y + 1];
    glyph[CHARSET_GLYPH_BYTES - 1] = first;
}

/**
 * Scroll a glyph's rows down by one pixel in place, wrapping the bottom
 * row to the top.
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_scroll_down(volatile uint8_t *glyph)
{
    uint8_t last = glyph[CHARSET_GLYPH_BYTES - 1];
    uint8_t y;
    for (y = CHARSET_GLYPH_BYTES - 1; y > 0; y--) glyph[y] = glyph[y - 1];
    glyph[0] = last;
}

/**
 * Rotate every row of a glyph left by one pixel in place, within the
 * CHARSET_GLYPH_WIDTH-bit pixel field (the bit that scrolls off the left
 * edge wraps to the right edge).
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_rotate_left(volatile uint8_t *glyph)
{
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++)
    {
        uint8_t v = (uint8_t)(glyph[y] << 1);
        if (glyph[y] & CHARSET_GLYPH_MSB) v |= 0x01;
        glyph[y] = v & CHARSET_GLYPH_MASK;
    }
}

/**
 * Rotate every row of a glyph right by one pixel in place, within the
 * CHARSET_GLYPH_WIDTH-bit pixel field (the bit that scrolls off the right
 * edge wraps to the left edge).
 *
 * @param glyph Pointer to the glyph's CHARSET_GLYPH_BYTES-byte bitmap.
 * @return (none)
 */
void charset_glyph_rotate_right(volatile uint8_t *glyph)
{
    uint8_t y;
    for (y = 0; y < CHARSET_GLYPH_BYTES; y++)
    {
        uint8_t v = glyph[y] >> 1;
        if (glyph[y] & 0x01) v |= CHARSET_GLYPH_MSB;
        glyph[y] = v;
    }
}
