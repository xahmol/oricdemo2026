// ijk.h - Raxiss IJK joystick interface for Oric Atmos (bare-metal, Oscar64)
//
// Based on: libsrc/ijk-driver.s by raxiss (c) 2021, GPL v3.
//   https://github.com/iss000/oricOpenLibrary
// Adapted: C port using Oscar64 VIA struct and __asm SEI/CLI.
//
// The IJK interface uses VIA Port A ($030F, no-handshake) and Port B bit 4
// (printer strobe) for joystick select. VIA Port A is shared with the
// keyboard scanner — always bracket IJK I/O with SEI/CLI.

#ifndef IJK_H
#define IJK_H

#include <stdint.h>
#include "oric.h"

// ─────────────────────────────────────────────────────────────────────────────
// Return-value bit masks for ijk_ljoy / ijk_rjoy
//
// VIA Port A bit layout (after active-low inversion in ijk_read):
//   PA0=RIGHT  PA1=LEFT  PA2=FIRE  PA3=DOWN  PA4=UP
// ─────────────────────────────────────────────────────────────────────────────

#define IJK_JOY_RIGHT   0x01
#define IJK_JOY_LEFT    0x02
#define IJK_JOY_FIRE    0x04
#define IJK_JOY_DOWN    0x08
#define IJK_JOY_UP      0x10

// ─────────────────────────────────────────────────────────────────────────────
// Global state (written by ijk_detect / ijk_read)
// ─────────────────────────────────────────────────────────────────────────────

extern uint8_t ijk_present;  // non-zero when IJK is detected
extern uint8_t ijk_ljoy;     // left  joystick state (IJK_JOY_* bits)
extern uint8_t ijk_rjoy;     // right joystick state (IJK_JOY_* bits)

// ─────────────────────────────────────────────────────────────────────────────
// Functions
// ─────────────────────────────────────────────────────────────────────────────

void ijk_detect(void);  // probe for IJK; sets ijk_present
void ijk_read(void);    // read both sticks into ijk_ljoy/ijk_rjoy (no-op if absent)

#pragma compile("ijk.c")

#endif
