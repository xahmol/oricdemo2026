// ijk.c - Raxiss IJK joystick driver for Oric Atmos (bare-metal, Oscar64)
//
// Based on: libsrc/ijk-driver.s by raxiss (c) 2021, GPL v3.
//   https://github.com/iss000/oricOpenLibrary
// Adapted: C port using Oscar64 VIA struct and __asm SEI/CLI.
//
// VIA Port A bit layout (no-handshake access via VIA.pra2 at $030F):
//   PA0=RIGHT  PA1=LEFT  PA2=FIRE  PA3=DOWN  PA4=UP
//   PA5=DETECT PA6=LSTICK-select PA7=RSTICK-select
// Buttons are active-low; ijk_read inverts so pressed=1, released=0.
// VIA Port B bit 4 = printer strobe (low = IJK active).
// SEI/CLI brackets all VIA Port A accesses to prevent interleaving with
// the keyboard scanner (keyboard.c also uses VIA.pra2 for AY bus writes).

#include "ijk.h"

uint8_t ijk_present = 0;
uint8_t ijk_ljoy    = 0;
uint8_t ijk_rjoy    = 0;

/**
 * Probe VIA Port A to determine whether an IJK joystick interface is
 * present and update the global ijk_present flag accordingly. Resets
 * ijk_ljoy and ijk_rjoy to 0. Brackets all VIA Port A access with PHP/SEI/
 * PLP so the keyboard scanner (which also uses VIA.pra2) cannot interleave.
 *
 * @return (none) -- result is written to the global ijk_present.
 */
void ijk_detect(void)
{
    uint8_t saved_ddra, saved_ora;

    // PHP/PLP, not SEI/CLI: oric_startup leaves IRQs permanently disabled
    // (no IRQ handler is installed). An unconditional CLI here would
    // re-enable IRQs for the rest of the program, letting the stock ROM
    // IRQ handler run every frame and corrupt zero page / screen RAM.
    __asm { php }
    __asm { sei }

    saved_ddra = VIA.ddra;
    saved_ora  = VIA.pra2;

    ijk_present = 0;
    ijk_ljoy    = 0;
    ijk_rjoy    = 0;

    // DDRA: PA7+PA6 = out (stick-select), PA5..PA0 = in
    VIA.ddra = 0xC0;
    // Strobe low (clear bit 4 of Port B) to activate IJK
    VIA.prb = (uint8_t)(VIA.prb & (uint8_t)~0x10);

    // Select both sticks (PA7=1, PA6=1) and sample PA5
    // PA5 = 0 when IJK is present; invert to get ijk_present = 1
    VIA.pra2 = 0xC0;
    ijk_present = (uint8_t)((VIA.pra2 & 0x20) ^ 0x20);

    // Strobe high: release IJK bus, restore state
    VIA.prb  = (uint8_t)(VIA.prb | 0x10);
    VIA.pra2 = saved_ora;
    VIA.ddra = saved_ddra;

    __asm { plp }
}

/**
 * Sample both joysticks via VIA Port A and update the global ijk_ljoy/
 * ijk_rjoy bitmasks (IJK_JOY_* bits, active-high after inversion). No-op if
 * ijk_present is 0 (no IJK interface was detected by ijk_detect()).
 * Brackets all VIA Port A access with PHP/SEI/PLP so the keyboard scanner
 * (which also uses VIA.pra2) cannot interleave.
 *
 * @return (none) -- results are written to the globals ijk_ljoy/ijk_rjoy.
 */
void ijk_read(void)
{
    uint8_t saved_ddra, saved_ora;

    if (!ijk_present) return;

    // PHP/PLP required — see ijk_detect() above.
    __asm { php }
    __asm { sei }

    saved_ddra = VIA.ddra;
    saved_ora  = VIA.pra2;

    VIA.ddra = 0xC0;
    VIA.prb  = (uint8_t)(VIA.prb & (uint8_t)~0x10);

    // Left joystick: select PA6 high, PA7 low
    VIA.pra2 = 0x40;
    ijk_ljoy  = (uint8_t)((VIA.pra2 & 0x1F) ^ 0x1F);

    // Right joystick: select PA7 high, PA6 low
    VIA.pra2 = 0x80;
    ijk_rjoy  = (uint8_t)((VIA.pra2 & 0x1F) ^ 0x1F);

    VIA.prb  = (uint8_t)(VIA.prb | 0x10);
    VIA.pra2 = saved_ora;
    VIA.ddra = saved_ddra;

    __asm { plp }
}
