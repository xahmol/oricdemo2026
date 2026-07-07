# Hardware overview (oric.h)

Include `oric.h` in any file that references hardware registers, attribute
constants, or screen layout macros.

### Screen

| Symbol | Value | Meaning |
|---|---|---|
| `TEXTVRAM` | `0xBB80` | Base address of text screen RAM |
| `SCREEN_COLS` | `40` | Columns per row |
| `SCREEN_ROWS` | `28` | Rows |
| `SCREEN_SIZE` | `1120` | Total bytes (40 × 28) |

The Oric ULA processes screen RAM left-to-right on every raster line. A byte
with `(byte & 0x60) == 0` (i.e. values `0x00–0x1F`) is a **serial attribute**
that changes ink colour, paper colour, or character set for the rest of that
row.  All other byte values are character codes.  The ULA resets ink to white
and paper to black at the start of each raster line.

**Consequence:** attributes occupy one screen column each and shift all
subsequent characters one position to the right.

### Ink (foreground) colour constants

`A_FWBLACK` (0) · `A_FWRED` (1) · `A_FWGREEN` (2) · `A_FWYELLOW` (3)
`A_FWBLUE` (4) · `A_FWMAGENTA` (5) · `A_FWCYAN` (6) · `A_FWWHITE` (7)

**Warning:** `A_FWBLACK = 0x00` is the C NUL terminator.  It cannot be
embedded in a string literal.  Write it with `cwin_put_attr(&w, A_FWBLACK)`.

### Paper (background) colour constants

`A_BGBLACK` (16) · `A_BGRED` (17) · `A_BGGREEN` (18) · `A_BGYELLOW` (19)
`A_BGBLUE` (20) · `A_BGMAGENTA` (21) · `A_BGCYAN` (22) · `A_BGWHITE` (23)

### Character-set mode constants

| Constant | Value | Effect |
|---|---|---|
| `A_STD` | 8 | Standard character set |
| `A_ALT` | 9 | Alternate (mosaic/graphics) set |
| `A_STD2H` | 10 | Double-height standard |
| `A_ALT2H` | 11 | Double-height alternate |
| `A_BLINKSTD` | 12 | Blinking standard |
| `A_BLINKALT` | 13 | Blinking alternate |
| `A_BLINK2H` | 14 | Double-height blinking |
| `A_BLINK2HALT` | 15 | Double-height blinking alternate |

### TEXT/HIRES mode-switch attributes

| Constant | Value | Effect |
|---|---|---|
| `A_TEXT_60HZ` | 24 | Switch to TEXT mode, 60Hz (NTSC) |
| `A_TEXT_50HZ` | 26 | Switch to TEXT mode, 50Hz (PAL) |
| `A_HIRES_60HZ` (`A_HIRES`) | 28 | Switch to HIRES mode, 60Hz (NTSC) |
| `A_HIRES_50HZ` | 30 | Switch to HIRES mode, 50Hz (PAL) |

Placing one of these as the **last column byte (column 39)** of whichever
mode currently governs a scanline switches the display mode starting the
*next* scanline — not an instant, whole-screen switch. Once written, the
trigger is **sticky**: it re-applies every frame until overwritten. See
[hires.md](hires.md)'s `hires_on()`/`hires_off()`/`hires_footer_enable()` for
the standard usage patterns and the full mechanism (entering HIRES sacrifices
a one-scanline sliver of TEXT row 0 every frame; a PAL Oric Atmos should use
the `_50HZ` variants).

### Special character codes

| Constant | Value | Character |
|---|---|---|
| `CH_SPACE` | `0x20` | Space — safe for clearing |
| `CH_INVSPACE` | `0xA0` | Solid ink-colour block (cursor, animation) |
| `CH_POUND` | `0x5F` | £ sign (ROM maps ASCII `_`) |
| `CH_COPYRIGHT` | `0x60` | © sign (ROM maps ASCII `` ` ``) |

Avoid `_` and `` ` `` in display strings; they render as £ and ©.

### Character set RAM (CHARSET_STD / CHARSET_ALT / CHARSETROM)

The Oric's ULA renders text glyphs from **live RAM**, not ROM — redefining
the bytes below takes effect on the very next raster line, no special
handling needed.

| Symbol | Value | Meaning |
|---|---|---|
| `CHARSET_STD` | `0xB400` | Standard charset bank base (codes `0x00`-`0x7F`), **TEXT mode only** |
| `CHARSET_ALT` | `0xB800` | Alternate charset bank base (codes `0x00`-`0x7F`), **TEXT mode only** |
| `CHARSETROM` | `0xFC78` | ROM source for the standard charset (96 printable codes only, see below) |
| `HIRES_CHARSET_STD` | `0x9800` | Standard charset bank base, **HIRES mode only** |
| `HIRES_CHARSET_ALT` | `0x9C00` | Alternate charset bank base, **HIRES mode only** |

Each bank is 1024 bytes = 128 glyphs (codes `0x00`-`0x7F`) × 8 bytes (one per
pixel row); bits 5..0 of each byte are pixels left-to-right (bit5 = leftmost
of the 6 visible columns — Oric chars are 6×8).

**Two different addressing conventions:**

- `CHARSET_STD`/`CHARSET_ALT`/`HIRES_CHARSET_STD`/`HIRES_CHARSET_ALT`: full
  128-glyph banks, glyph address = `base + screencode*8`, **no offset**.
  Codes `0x00-0x1F` are present in RAM but never displayed (those screen-RAM
  byte values trigger attribute mode instead of a glyph lookup).
- `CHARSETROM`: only the 96 printable codes `0x20-0x7F` (768 bytes), glyph
  address = `CHARSETROM + (screencode-0x20)*8` — i.e. **with** a `-0x20`
  offset. This is the source for the "restore standard charset" command
  (standard set only), and for [hires.md](hires.md)'s `hb_put_chars()`.

**Why HIRES mode needs its own charset bank addresses:** while HIRES mode
governs the screen, `CHARSET_STD`/`CHARSET_ALT`'s normal address range
(`$B400-$BBFF`) is reused as HIRES bitmap data — the ULA reads charset RAM
from `HIRES_CHARSET_STD`/`HIRES_CHARSET_ALT` (`$9800-$9FFF`) instead, for the
built-in 3-row TEXT footer at the bottom of a HIRES screen. See
[hires.md](hires.md) for the full HIRES memory map.

[charset.md](charset.md) provides addressing/copy primitives and glyph-bitmap
transforms over the TEXT-mode layout.

### Inline string attribute macros (ASTR_*)

Embed colour or charset changes directly in string literals:

```c
cwin_putat_string(&w, 2, 5,
    ASTR_INK_RED "alert" ASTR_INK_WHITE " normal");
```

Each `ASTR_*` expands to a one-byte escape sequence.  Available macros:

- Ink: `ASTR_INK_RED` · `_GREEN` · `_YELLOW` · `_BLUE` · `_MAGENTA` · `_CYAN` · `_WHITE`
- Paper: `ASTR_PAPER_BLACK` · `_RED` · `_GREEN` · `_YELLOW` · `_BLUE` · `_MAGENTA` · `_CYAN` · `_WHITE`
- Charset: `ASTR_CHARSET_STD` · `_ALT` · `_STD2H` · `_ALT2H` · `_BLKSTD` · `_BLKALT` · `_BLK2H` · `_BLK2HA`

**Constraints:**
- `ASTR_INK_BLACK` (`\x00`) = NUL — cannot appear in a C string literal.
  Use `cwin_put_attr(&w, A_FWBLACK)`.
- `ASTR_CHARSET_STD2H` (`\x0A`) = `\n` — triggers scroll in
  `cwin_console_put_string`. Use `cwin_put_attr` in console context.

### Overlay RAM

Overlay RAM (`0xC000–0xFFFF`) is normally the Atmos ROM.  When LOCI is
connected, writing to the Microdisc-compatible register at `0x0314` enables
RAM underneath the ROM.

```c
#define MICRODISCCFG    (*((volatile uint8_t *)0x0314))
#define OVERLAY_ON      0xFD   // enable overlay RAM
#define OVERLAY_OFF     0xFF   // restore ROM
#define OVERLAY_BASE    0xC000U
#define OVERLAY_SIZE    0x4000U
```

**Requires LOCI.** Not testable in Oricutron.  The high-level helpers
`enable_overlay_ram()` and `disable_overlay_ram()` in
[loci.md](loci.md) are the preferred interface.
