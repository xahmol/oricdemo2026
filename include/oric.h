// oric.h - Oric Atmos hardware constants and register definitions
// For use with Oscar64 compiler, bare-metal target

#ifndef ORIC_H
#define ORIC_H

#include <stdint.h>
#include <stdbool.h>

// -------------------------------------------------------------------------
// VIA 6522 at $0300
// -------------------------------------------------------------------------

typedef volatile struct {
    uint8_t prb;    // $0300  Port B: bits 0-2=row select, 3=tape/kbd-sense, 4=printer strobe,
                    //                5=cassette motor, 6=AY BC1, 7=AY BDIR
    uint8_t pra;    // $0301  Port A (with handshake): AY data bus / printer data
    uint8_t ddrb;   // $0302  DDR B: ROM sets to $F7 (bit 3 = input, rest output)
    uint8_t ddra;   // $0303  DDR A
    uint8_t t1lo;   // $0304  Timer 1 counter low
    uint8_t t1hi;   // $0305  Timer 1 counter high / start
    uint8_t t1llo;  // $0306  Timer 1 latch low
    uint8_t t1lhi;  // $0307  Timer 1 latch high
    uint8_t t2lo;   // $0308  Timer 2 low
    uint8_t t2hi;   // $0309  Timer 2 high
    uint8_t sr;     // $030A  Shift register
    uint8_t acr;    // $030B  Auxiliary control register
    uint8_t pcr;    // $030C  Peripheral control register (CB2 = AY column drive)
    uint8_t ifr;    // $030D  Interrupt flag register (bit 6 = Timer 1)
    uint8_t ier;    // $030E  Interrupt enable register
    uint8_t pra2;   // $030F  Port A without handshake (use for AY data bus writes)
} VIA_t;

#define VIA (*((VIA_t *)0x0300))

// VIA Port B initial value (ROM sets DDRB = $F7; bit 3 always input)
#define VIA_DDRB_INIT   0xF7

// -------------------------------------------------------------------------
// AY-3-8912 control sequences (via VIA Port B bits 6-7 = BC1/BDIR)
//
// Write sequence for AY register N = value V:
//   1. VIA.pra2 = N;  VIA.prb |= 0xC0;          (BDIR=1, BC1=1 = latch address)
//   2. VIA.prb  = (VIA.prb & 0x3F) | 0x80;      (BDIR=1, BC1=0 = write data)
//      VIA.pra2 = V;
//   3. VIA.prb &= 0x3F;                           (BDIR=0, BC1=0 = inactive)
//
// PCR ($030C) is separately used for CB2 = AY keyboard column drive assertion.
// PCR bits 7-5: 111=CB2 high (deassert), 110=CB2 low (assert)
// -------------------------------------------------------------------------

// AY register numbers
#define AY_REG_MIXER    7    // Mixer control (enable/disable tone+noise per channel)
#define AY_REG_IOA      14   // External Port A (keyboard column drive, active-low)
#define AY_REG_IOB      15   // External Port B (not connected on Oric)

// -------------------------------------------------------------------------
// Screen
// -------------------------------------------------------------------------

#define TEXTVRAM        0xBB80U    // Text screen RAM: 40 columns × 28 rows
#define SCREEN_COLS     40
#define SCREEN_ROWS     28
#define SCREEN_SIZE     (SCREEN_COLS * SCREEN_ROWS)   // 1120 bytes

// -------------------------------------------------------------------------
// Character set RAM ($B400-$BBFF -- see include/oric_crt.c memory layout)
//
// Live, ULA-rendered charset RAM. Each bank is 1024 bytes = 128 glyphs
// (codes 0x00-0x7F) x 8 bytes (one per pixel row); bits 5..0 of each byte
// are pixels left-to-right (bit5 = leftmost of the 6 visible columns --
// Oric chars are 6x8). Glyph address = base + code*8 (NO offset -- codes
// 0x00-0x1F are present in RAM but never displayed, since those screen-RAM
// byte values trigger attribute mode instead of a glyph lookup). Redefining
// bytes here takes effect on the next raster line, no special handling needed.
// -------------------------------------------------------------------------

#define CHARSET_STD     0xB400U    // Standard charset bank base (codes 0x00-0x7F)
#define CHARSET_ALT     0xB800U    // Alternate charset bank base (codes 0x00-0x7F)

// ROM source for the standard charset ('restore from ROM' command, std only).
// NOTE the different offset convention: the ROM table only covers the 96
// printable codes 0x20-0x7F (768 bytes), so glyph address =
// CHARSETROM + (screencode - 0x20) * 8 -- unlike CHARSET_STD/CHARSET_ALT
// above, which have NO -0x20 offset (full 128-glyph banks, codes 0x00-0x7F).
#define CHARSETROM      0xFC78U

// -------------------------------------------------------------------------
// HIRES bitmap ($A000-$BF3F) -- see include/hires.h for the drawing library.
//
// 40 bytes/scanline x 200 scanlines, simple linear raster (byte address =
// HIRESVRAM + y*HIRES_ROW_BYTES + x/6 -- NOT cell-interleaved like a C64
// bitmap). Pixel byte: bit7 = invert, bit6 MUST be 1 (else the ULA reads
// the byte as a serial attribute per the (byte & 0x60) == 0 rule below),
// bits5-0 = 6 pixels, bit5 = leftmost. All-ink byte = 0x7F, all-paper = 0x40.
//
// $BF40-$BF67 (42 bytes) are unused, then a built-in 3-line TEXT footer at
// HIRES_FOOTER ($BF68-$BFDF, 120 bytes), rendered with normal TEXT-mode
// rules using the HIRES-mode charset banks below.
//
// While HIRES mode governs the screen, charset RAM relocates: it does NOT
// stay at CHARSET_STD/CHARSET_ALT above (that address range is reused as
// HIRES bitmap data) -- use HIRES_CHARSET_STD/ALT instead.
// -------------------------------------------------------------------------

#define HIRESVRAM           0xA000U   // Base of the 8000-byte HIRES bitmap
#define HIRES_ROW_BYTES     40
#define HIRES_ROWS          200
#define HIRES_WIDTH_PX      240        // HIRES_ROW_BYTES * 6
#define HIRES_SIZE          8000U      // HIRES_ROW_BYTES * HIRES_ROWS

#define HIRES_FOOTER_UNUSED 0xBF40U    // 42 unused bytes before the footer
#define HIRES_FOOTER        0xBF68U    // Built-in 3-line TEXT footer
#define HIRES_FOOTER_ROWS   3

#define HIRES_CHARSET_STD   0x9800U    // Standard charset bank base (HIRES mode only)
#define HIRES_CHARSET_ALT   0x9C00U    // Alternate charset bank base (HIRES mode only)

// -------------------------------------------------------------------------
// Serial attribute codes (write to screen RAM with bit 6 = 0)
// A byte in screen RAM with bit 6 = 0: serial attribute, affects rest of row
// A byte in screen RAM with bit 6 = 1: display character from character ROM
// -------------------------------------------------------------------------

// Foreground (INK) colors — values 0–7
#define A_FWBLACK       0
#define A_FWRED         1
#define A_FWGREEN       2
#define A_FWYELLOW      3
#define A_FWBLUE        4
#define A_FWMAGENTA     5
#define A_FWCYAN        6
#define A_FWWHITE       7

// Background (PAPER) colors — values 16–23
#define A_BGBLACK       16
#define A_BGRED         17
#define A_BGGREEN       18
#define A_BGYELLOW      19
#define A_BGBLUE        20
#define A_BGMAGENTA     21
#define A_BGCYAN        22
#define A_BGWHITE       23

// Character mode switches (write to screen RAM as attributes)
#define A_STD           8    // Standard character set
#define A_ALT           9    // Alternate (mosaic/graphics) character set
#define A_STD2H        10    // Double-height standard charset
#define A_ALT2H        11    // Double-height alternate charset
#define A_BLINKSTD     12    // Blinking standard charset
#define A_BLINKALT     13    // Blinking alternate charset
#define A_BLINK2H      14    // Double-height blinking standard charset
#define A_BLINK2HALT   15    // Double-height blinking alternate charset

// -------------------------------------------------------------------------
// TEXT/HIRES mode-switch attributes.
//
// Placing one of these as the LAST column byte (column 39) of whichever
// mode currently governs a scanline switches the display mode starting the
// NEXT scanline -- it is not an instant, whole-screen switch. See
// include/hires.h's hires_on()/hires_off()/hires_footer_enable() for the
// standard usage patterns (poke at TEXTVRAM+39 to enter HIRES from a TEXT
// screen; poke at HIRESVRAM+196*HIRES_ROW_BYTES+39 to carve out the 3-line
// HIRES_FOOTER at the bottom of a HIRES screen).
//
// Each pair (e.g. 24/25) is a duplicate in the ULA; the 50Hz/60Hz distinction
// matters for PAL vs NTSC hardware (Oric Atmos in Europe is PAL/50Hz).
// -------------------------------------------------------------------------

#define A_TEXT_60HZ    24    // Switch to TEXT mode, 60Hz (NTSC)
#define A_TEXT_50HZ    26    // Switch to TEXT mode, 50Hz (PAL)
#define A_HIRES_60HZ   28    // Switch to HIRES mode, 60Hz (NTSC) -- same as A_HIRES
#define A_HIRES_50HZ   30    // Switch to HIRES mode, 50Hz (PAL)

#define A_HIRES        28    // Switch to HIRES mode (do not use in text apps)

// -------------------------------------------------------------------------
// ASTR_* — serial attribute bytes for embedding in string literals.
//
// Usage: cwin_putat_string(&w, x, y, ASTR_INK_RED "red text" ASTR_INK_WHITE "white");
//
// How it works: cwin_putat_string writes bytes as-is.  The Oric ULA treats
// any byte with (byte & 0x60) == 0 (i.e. values 0x00–0x1F) as a serial
// attribute that changes ink/paper/charset for the rest of that raster line.
// The attribute byte occupies one screen column (displays as a paper-colour box).
//
// Constraints:
//   - ASTR_INK_BLACK (\x00) = NUL, CANNOT appear in a C string literal.
//     Use cwin_put_attr(&w, A_FWBLACK) instead.
//   - ASTR_CHARSET_STD2H (\x0A) = '\n', triggers scroll in console-mode
//     output (cwin_console_put_string). Use cwin_put_attr() there instead.
//   - All ASTR_* values work correctly with cwin_putat_string (positional).
// -------------------------------------------------------------------------

// Inline ink color (values 1–7; 0 = black cannot be embedded, see above)
#define ASTR_INK_RED        "\x01"
#define ASTR_INK_GREEN      "\x02"
#define ASTR_INK_YELLOW     "\x03"
#define ASTR_INK_BLUE       "\x04"
#define ASTR_INK_MAGENTA    "\x05"
#define ASTR_INK_CYAN       "\x06"
#define ASTR_INK_WHITE      "\x07"

// Inline charset mode (8–15; see \x0A console caveat above)
#define ASTR_CHARSET_STD    "\x08"
#define ASTR_CHARSET_ALT    "\x09"
#define ASTR_CHARSET_STD2H  "\x0A"
#define ASTR_CHARSET_ALT2H  "\x0B"
#define ASTR_CHARSET_BLKSTD "\x0C"
#define ASTR_CHARSET_BLKALT "\x0D"
#define ASTR_CHARSET_BLK2H  "\x0E"
#define ASTR_CHARSET_BLK2HA "\x0F"

// Inline paper color (16–23)
#define ASTR_PAPER_BLACK    "\x10"
#define ASTR_PAPER_RED      "\x11"
#define ASTR_PAPER_GREEN    "\x12"
#define ASTR_PAPER_YELLOW   "\x13"
#define ASTR_PAPER_BLUE     "\x14"
#define ASTR_PAPER_MAGENTA  "\x15"
#define ASTR_PAPER_CYAN     "\x16"
#define ASTR_PAPER_WHITE    "\x17"

// Oric ULA character/attribute detection: (byte & 0x60) == 0 → serial attribute.
// Attributes are only bytes 0x00–0x1F.  All other byte values are characters.
// Notable Oric ROM differences from standard ASCII:
//   0x20 (' ')  → space character (safe for clearing — NOT an attribute)
//   0x5F ('_')  → £  (avoid in display strings; use CH_POUND if intentional)
//   0x60 ('`')  → ©  (avoid; was mistakenly called "blank" in earlier comments)
// Use CH_SPACE (0x20) for clearing/erasing; use CH_INVSPACE for the cursor block.
#define CH_ORIC(c)      ((uint8_t)((c) | 0x40))
#define CH_SPACE        0x20    // Space — standard blank character, safe for clearing
#define CH_INVSPACE     0xA0    // Inverse space (solid ink-color block; used for cursor)
#define CH_POUND        0x5F    // £ sign (Oric ROM maps ASCII underscore position)
#define CH_COPYRIGHT    0x60    // © sign (Oric ROM maps ASCII backtick/grave position)

// -------------------------------------------------------------------------
// Overlay RAM at $C000–$FFFF
//
// Normally this region is occupied by the Atmos ROM (16 KB).
// When LOCI is connected and acting as disk controller, the register at
// $0314 (Microdisc-compatible) can enable overlay RAM underneath the ROM.
//
// NOTE: Overlay RAM requires LOCI active. Not available in Oricutron.
// Reference: https://osdk.org/index.php?page=articles&ref=ART14
// -------------------------------------------------------------------------

#define MICRODISCCFG    (*((volatile uint8_t *)0x0314))
#define OVERLAY_ON      0xFD   // %11111101 — enable overlay RAM (LOCI required)
#define OVERLAY_OFF     0xFF   // %11111111 — restore ROM

#define OVERLAY_BASE    0xC000U   // Start of overlay RAM
#define OVERLAY_SIZE    0x4000U   // 16 KB

// -------------------------------------------------------------------------
// Oric ROM system vectors (do not call when overlay RAM is active)
// -------------------------------------------------------------------------

#define XGETKY          ((void (*)())0x023B)  // Patchable keyboard vector → GTORKB
#define GTORKB          0xEB78                // Get key with auto-repeat (ROM entry)

// IRQ RAM vector (ROM chains through here; we install our handler here)
#define IRQ_VEC_LO      (*((volatile uint8_t *)0x0245))
#define IRQ_VEC_HI      (*((volatile uint8_t *)0x0246))

// -------------------------------------------------------------------------
// VIA Timer 1 (100 Hz system IRQ)
// -------------------------------------------------------------------------

// ROM sets Timer 1 in free-run mode for 100 Hz (latch value ≈ 9984)
// For custom timer: write latch to $0306/$0307, start with write to $0305
#define TIMER1_100HZ    9984    // Latch value for 100 Hz @ 1 MHz Oric clock

#endif  // ORIC_H
