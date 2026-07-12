// satellite.h - small "satellite" sprite for src/section_sprite_showcase.c
//
// Originally created for this project (not adapted from any external
// source): a simple rectangular body, two solar-panel wings, an antenna,
// and a small nose dish -- 24x16px (4 bytes/row x 16 rows), tightly packed
// in this project's own hires.c byte convention (bit6 set on every byte,
// bits5-0 = 6 pixels, 0x40 = blank), matching sprite.h's hxspr_draw()'s
// expected raw image format exactly (same convention assets/bird.h uses).
// Generated via a one-off Python/PIL script (drawn with basic rectangle/
// line primitives, then packed with the same bit-packing tools/
// oric_pictconv.py's own convert_mono() uses), not hand-authored bytes.

#ifndef SATELLITE_H
#define SATELLITE_H

#define SATELLITE_W_BYTES 4u
#define SATELLITE_H        16u

static const unsigned char satellite_sprite[SATELLITE_W_BYTES * SATELLITE_H] = {
    0x40, 0x40, 0x40, 0x40, 0x40, 0x41, 0x40, 0x40, 0x40, 0x41, 0x40, 0x40, 0x40, 0x41, 0x40, 0x40,
    0x40, 0x41, 0x40, 0x40, 0x40, 0x47, 0x78, 0x40, 0x5f, 0x77, 0x7b, 0x7e, 0x5f, 0x77, 0x7b, 0x7e,
    0x5f, 0x77, 0x7b, 0x7e, 0x5f, 0x77, 0x7b, 0x7e, 0x40, 0x47, 0x78, 0x40, 0x40, 0x41, 0x60, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
};

#endif // SATELLITE_H
