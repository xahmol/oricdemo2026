// ay.h - AY-3-8912 register-write helper for Oric Atmos (bare-metal, Oscar64)
//
// The correct write protocol was confirmed by two independent, mutually-
// agreeing sources: this project's own tested include/keyboard.c (which
// already does raw AY register selects for keyboard column drive), and
// 6502Nerd/dflat's PT3 player (ROUT routine, Oric/software/project/pt3/
// ppt3.s) -- NOT the VIA Port B bits 6-7 scheme an earlier version of
// include/oric.h's own comment incorrectly claimed.
//
// See include/oric.h for the AY_REG_* register constants and the write
// sequence this implements.

#ifndef AY_H
#define AY_H

#include <stdint.h>

// Selects AY register `reg` and writes `value` to it. Brackets the whole
// VIA/PCR sequence with PHP/SEI/PLP (matching include/ijk.c's convention,
// not a bare SEI/CLI) so this cannot interleave with include/keyboard.c's
// own AY access, and so it stays safe to call from an __interrupt context
// (see include/pt3.h) where interrupts are already disabled by the calling
// __hwinterrupt handler -- PHP/PLP preserves whatever the interrupt flag
// already was rather than assuming it.
void ay_write(uint8_t reg, uint8_t value);

#pragma compile("ay.c")

#endif // AY_H
