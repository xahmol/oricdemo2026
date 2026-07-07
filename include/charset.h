// charset.h - Generic Oric charset-bank addressing and copy primitives
//
// Pure addressing/copy helpers for the Oric Atmos charset RAM model
// (CHARSET_STD/CHARSET_ALT/CHARSETROM, see oric.h) -- no application state,
// reusable across any Oric Atmos project that redefines charset RAM at
// runtime.
//
// Layout recap (full details in oric.h): each bank (CHARSET_STD=$B400,
// CHARSET_ALT=$B800) is a full 128-glyph table (codes 0x00-0x7F, 8
// bytes/glyph, addr=base+code*8). The displayable range (codes 0x20-0x7F,
// 96 glyphs = 768 bytes) lives at base+CHARSET_GLYPH_AREA_OFFSET and is the
// portion CHARSETROM mirrors, but CHARSETROM uses the opposite addressing
// convention: CHARSETROM+(code-0x20)*8 (no +0x100 offset, table starts at
// code 0x20).

#ifndef CHARSET_H
#define CHARSET_H

#include <stdint.h>

// Displayable glyph range within a charset bank (codes 0x20-0x7F).
#define CHARSET_GLYPH_AREA_OFFSET 0x100
#define CHARSET_GLYPH_AREA_SIZE   768

// CHARSET_ALT only has 896 bytes of real RAM ($B800-$BB7F) before screen
// RAM begins at $BB80 -- unlike CHARSET_STD ($B400-$B7FF), a full
// non-overlapping 1024-byte bank. So CHARSET_ALT's displayable range
// (base+CHARSET_GLYPH_AREA_OFFSET) can only safely span 640 bytes (codes
// 0x20-0x6F, 80 glyphs), not the full 768 -- codes 0x70-0x7F have no
// independent storage there at all; reading/writing past 640 bytes hits
// live screen RAM. charset_save()/charset_load() apply this automatically
// when base==CHARSET_ALT (see charset.c). This is also why the character
// editor's ce_max_code() and V1's own visualchar[] both already cap the
// Alt charset at code 0x6F.
#define CHARSET_ALT_GLYPH_AREA_SIZE 640

// Dimensions of a single Oric glyph: 8 bytes (one per pixel row), 6 visible
// pixel columns per row (bits 5..0, bit5 = leftmost). Fixed by the hardware.
#define CHARSET_GLYPH_BYTES  8
#define CHARSET_GLYPH_WIDTH  6

// Compute the charset-RAM address of screencode's 8-byte glyph data
// (address = base + screencode*8, codes 0x00-0x7F).
// altorstd: 0 = standard charset (CHARSET_STD), 1 = alternate (CHARSET_ALT).
uint16_t charset_address(uint8_t screencode, uint8_t altorstd);

// Displayable-range byte count for charset bank `base`: CHARSET_ALT_
// GLYPH_AREA_SIZE (640) if base==CHARSET_ALT, else CHARSET_GLYPH_AREA_SIZE
// (768). Used internally by charset_save()/charset_load(); also exported
// for callers (src/fileio.c) that copy a charset bank's displayable range
// directly without going through those two functions.
uint16_t charset_area_size(uint16_t base);

// Copy charset bank `base`'s displayable glyph range (charset_area_size()
// bytes -- CHARSET_ALT only copies 640, see CHARSET_ALT_GLYPH_AREA_SIZE)
// into dest[].
void charset_save(uint16_t base, uint8_t *dest);

// Copy charset_area_size(base) bytes from src[] into charset bank `base`'s
// displayable glyph range (CHARSET_ALT only writes 640, see
// CHARSET_ALT_GLYPH_AREA_SIZE). src may be another charset bank or
// CHARSETROM.
void charset_load(uint16_t base, const uint8_t *src);

// Pointer to screencode's 8-byte glyph in CHARSETROM (codes 0x20-0x7F only --
// the ROM table covers just the 96 printable codes, addressed as
// CHARSETROM+(screencode-0x20)*8, unlike charset_address()'s no-offset
// convention).
const uint8_t *charset_rom_glyph(uint8_t screencode);

// -------------------------------------------------------------------------
// Glyph-bitmap operations -- in-place transforms on an 8-byte Oric glyph
// (CHARSET_GLYPH_BYTES rows x CHARSET_GLYPH_WIDTH visible bits/row, bit5 =
// leftmost). `glyph` is typically a pointer into live charset RAM (see
// charset_address()), hence volatile.
// -------------------------------------------------------------------------

// Invert all pixels (XOR each row with the CHARSET_GLYPH_WIDTH-bit mask).
void charset_glyph_invert(volatile uint8_t *glyph);

// Mirror vertically: reverse the order of the 8 rows.
void charset_glyph_mirror_v(volatile uint8_t *glyph);

// Mirror horizontally: reverse the bit order of each row.
void charset_glyph_mirror_h(volatile uint8_t *glyph);

// Scroll all rows up by one, wrapping row 0 to the bottom.
void charset_glyph_scroll_up(volatile uint8_t *glyph);

// Scroll all rows down by one, wrapping the bottom row to row 0.
void charset_glyph_scroll_down(volatile uint8_t *glyph);

// Rotate each row's bits left by one (bit5 wraps to bit0).
void charset_glyph_rotate_left(volatile uint8_t *glyph);

// Rotate each row's bits right by one (bit0 wraps to bit5).
void charset_glyph_rotate_right(volatile uint8_t *glyph);

#pragma compile("charset.c")

#endif // CHARSET_H
