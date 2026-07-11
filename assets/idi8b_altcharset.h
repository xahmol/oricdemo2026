// idi8b_altcharset.h - the 3 ALT-charset glyph bitmaps idi8b_logo[]'s
// mosaic wordmark actually uses (codes 0x21, 0x30, 0x35 -- confirmed by
// scanning idi8b_logo[] for every character code >= 0x20 it contains).
//
// Captured empirically via a Phosphoric RAM dump of CHARSET_ALT ($B800 +
// code*8) during a real, correctly-rendering run of the ORIGINAL
// TEXT-mode splash on the tape/LOCI target (see git history) -- NOT
// derived from any documented "semigraphics bit convention": Oric's
// ALT-charset bank is plain redefinable RAM, not a fixed algorithmic
// mapping from code value to shape, so these bytes are the only source
// of truth for what these 3 codes are actually meant to look like.
// Each is 8 bytes (one per scanline), bits 5-0 = the 6 visible pixel
// columns (bit5 = leftmost), same format as CHARSETROM/rom_charset.h --
// all three happen to use only the LEFT 3 of 6 columns (0x38 = 0b111000),
// which is why two of them (0x35 tiled against itself) produce the
// wordmark's own dithered/striped look rather than solid blocks: 0x35 is
// solid over its own left half, blank over its right half, and adjacent
// mosaic cells alternate which half is "on".
//
// Copied into HIRES_CHARSET_ALT (at their own code*8 offset, matching
// charset_address()'s addressing convention) once at boot, right after
// hires_on() -- see main.c -- same "charset copied to RAM at boot"
// treatment as rom_charset.h's standard-charset copy, just for the ALT
// bank. src/section_splash.c reads these back from HIRES_CHARSET_ALT
// (not this array directly) when drawing the dissolve, exactly like a
// real Oric TEXT-mode ALT-charset consumer would.

#ifndef IDI8B_ALTCHARSET_H
#define IDI8B_ALTCHARSET_H

typedef struct
{
    unsigned char code;
    unsigned char glyph[8];
} Idi8bAltGlyph;

static const Idi8bAltGlyph idi8b_altcharset[3] = {
    { 0x21, { 0x38, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00 } },   // top half
    { 0x30, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x38, 0x38 } },   // bottom half
    { 0x35, { 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38 } },   // full height
};

#endif // IDI8B_ALTCHARSET_H
