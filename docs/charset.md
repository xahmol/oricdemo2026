# Generic charset library (charset.h)

Pure addressing/copy helpers for the Oric Atmos charset-RAM model
(`CHARSET_STD`/`CHARSET_ALT`/`CHARSETROM`, see [oric.md](oric.md)) — no
application state, reusable across any Oric Atmos project that redefines
charset RAM at runtime. Include `charset.h`; the library auto-compiles
`charset.c` via `#pragma compile`.

### Constants

| Constant | Value | Meaning |
|---|---|---|
| `CHARSET_GLYPH_AREA_OFFSET` | `0x100` | Offset of the displayable range (codes `0x20-0x7F`) within a `CHARSET_STD`/`CHARSET_ALT` bank |
| `CHARSET_GLYPH_AREA_SIZE` | `768` | Size of the displayable range (96 glyphs × 8 bytes) |
| `CHARSET_GLYPH_BYTES` | `8` | Bytes per glyph (one per pixel row) |
| `CHARSET_GLYPH_WIDTH` | `6` | Visible pixel columns per row (bits 5..0, bit5 = leftmost) |

### Addressing and copy primitives

```c
uint16_t charset_address(uint8_t screencode, uint8_t altorstd);
```
Compute the charset-RAM address of `screencode`'s 8-byte glyph
(`base + screencode*8`, codes `0x00-0x7F`). `altorstd`: `0` = `CHARSET_STD`,
`1` = `CHARSET_ALT`.

```c
void charset_save(uint16_t base, uint8_t *dest);
```
Copy the displayable glyph range (`CHARSET_GLYPH_AREA_SIZE` bytes) from
charset bank `base` (`CHARSET_STD` or `CHARSET_ALT`) into `dest[]`.

```c
void charset_load(uint16_t base, const uint8_t *src);
```
Copy `CHARSET_GLYPH_AREA_SIZE` bytes from `src[]` into charset bank `base`'s
displayable glyph range. `src` may be another charset bank or `CHARSETROM`.

```c
const uint8_t *charset_rom_glyph(uint8_t screencode);
```
Pointer to `screencode`'s 8-byte glyph in `CHARSETROM` (codes `0x20-0x7F`
only — addressed as `CHARSETROM + (screencode-0x20)*8`, the **opposite**
offset convention from `charset_address()`). Used by
[hires.md](hires.md)'s `hb_put_chars()` to render text in HIRES mode.

### Glyph-bitmap operations

In-place transforms on an 8-byte Oric glyph (`CHARSET_GLYPH_BYTES` rows ×
`CHARSET_GLYPH_WIDTH` visible bits/row, bit5 = leftmost). `glyph` is
typically a pointer into live charset RAM (see `charset_address()`), hence
`volatile`.

```c
void charset_glyph_invert(volatile uint8_t *glyph);
```
Invert all pixels (XOR each row with the 6-bit mask `0x3F`).

```c
void charset_glyph_mirror_v(volatile uint8_t *glyph);
```
Mirror vertically: reverse the order of the 8 rows.

```c
void charset_glyph_mirror_h(volatile uint8_t *glyph);
```
Mirror horizontally: reverse the bit order of each row.

```c
void charset_glyph_scroll_up(volatile uint8_t *glyph);
```
Scroll all rows up by one, wrapping row 0 to the bottom.

```c
void charset_glyph_scroll_down(volatile uint8_t *glyph);
```
Scroll all rows down by one, wrapping the bottom row to row 0.

```c
void charset_glyph_rotate_left(volatile uint8_t *glyph);
```
Rotate each row's bits left by one (bit5 wraps to bit0).

```c
void charset_glyph_rotate_right(volatile uint8_t *glyph);
```
Rotate each row's bits right by one (bit0 wraps to bit5).
