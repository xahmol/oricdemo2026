// buildtest.c - oricdemo2026 build-chain regression test
//
// Standalone TEXT-mode smoke test for the Oscar64/Oric build chain (default
// include/oric_crt.c runtime): initialises the character-window library,
// keyboard scanner, IJK joystick probe, and LOCI storage probe, then
// verifies Arkos (.aky) decode correctness against a synthetic fixture.
// Exercised by `make test` (see tests/scripts/test_boot.sh) -- not demo
// content.
//
// Moved out of src/main.c so main.c could become the real (HIRES-runtime)
// demo entry point without losing this regression coverage. Used to test
// PT3 (Vortex Tracker) instead -- that player is archived on the `pt3`
// branch (see ARCHIVE_NOTE.md there) after replacement by Arkos; see
// docs/arkos.md for why.

#include "oric.h"
#include "charwin.h"
#include "keyboard.h"
#include "ijk.h"
#include "loci.h"
#include "arkos.h"

int main(void)
{
    charwin_init();
    ijk_detect();

    OricCharWin scr;
    cwin_init(&scr, 2, 0, 38, 28, A_FWWHITE, A_BGBLACK);
    cwin_clear(&scr);

    cwin_putat_string(&scr, 0, 2, "ORIC DEMO 2026");
    cwin_putat_string(&scr, 0, 4, "Oscar64 build chain OK");

    if (loci_present())
        cwin_putat_string(&scr, 0, 6, "LOCI device detected");
    else
        cwin_putat_string(&scr, 0, 6, "No LOCI device found");

    if (ijk_present)
        cwin_putat_string(&scr, 0, 7, "IJK joystick detected");
    else
        cwin_putat_string(&scr, 0, 7, "No IJK joystick found");

    // Arkos player smoke test: tests/fixtures/arkos_test.aky is a tiny
    // synthetic module (not a real tune, 35 bytes, exported for $C000) --
    // one Linker pattern (duration 4), all 3 channels sharing a single
    // 4-byte RegisterBlock stream (exercises track/registerblock reuse, a
    // real compression technique in real .aky files -- see docs/arkos.md),
    // with an end-of-song entry looping back to itself. The 4-byte stream
    // decodes as one INITIAL frame (NoSoftNoHard path: sets volume, closes
    // the tone bit) followed by three NON-INITIAL frames continuing from
    // wherever the previous frame's cursor left off (NoSoftNoHard-or-loop
    // path, exercising 3 different mask values, deliberately avoiding the
    // "loop" mask value itself to keep this fixture a simple straight-line
    // decode -- the real loop-back mechanism is exercised at the Linker
    // level instead, by pattern 1's own end-of-song marker). Built and
    // hand-verified against the same instruction-level decode replica used
    // to validate arkos.c itself -- see docs/arkos.md's Verification
    // section for the exact expected register values below. Byte values
    // (0x08, 0x00, 0x04, 0x0C) were re-derived after a real dispatch-
    // condition-inversion bug was found and fixed in the SOFTWAREONLY-vs-
    // SOFTWAREANDHARDWARE branch (see docs/arkos.md) -- this fixture alone
    // never exercised those two paths, so it did NOT catch that bug; the
    // fix was found by decoding the real shipped song instead. Expected
    // mixer value (0x3F, not 0x1F) also reflects a SECOND real bug fix,
    // in the r7 shift-accumulator's timing (see arkos_tick()'s own
    // comment) -- also never caught by this fixture, since a single
    // wrong-but-consistent mixer computation looks the same whether
    // computed 2 or 3 shifts deep; only decoding the real song and
    // checking each channel's own tone/noise state against its own
    // volume activity exposed it.
    if (arkos_load("arkos_test.aky"))
    {
        const uint8_t *shadow;
        uint8_t i;
        arkos_init();
        arkos_tick();
        shadow = arkos_debug_shadow();
        cwin_putat_string(&scr, 0, 8, "Arkos tune loaded, AY regs:");
        for (i = 0; i < 14; i++)
            cwin_putat_printf(&scr, (uint8_t)(i * 3), 9, "%02x", shadow[i]);

        // Ticks 2-4: three non-initial RegisterBlock frames (bytes 0x00,
        // 0x04, 0x0C -- see the fixture comment above), landing on a known
        // final AY register snapshot (R7=0x3F, R8/R9/R10=0x01 -- same as
        // tick 1's own value, by coincidence of this fixture's chosen
        // bytes, not because nothing happened in between: tick 2 writes
        // nothing, tick 3 writes 0, tick 4 writes back to 1). Other
        // registers stay unchanged from tick 1's shadow -- this fixture's
        // byte pattern never touches tone-period or hardware-envelope
        // registers.
        for (i = 0; i < 3; i++)
            arkos_tick();
        shadow = arkos_debug_shadow();
        cwin_putat_string(&scr, 0, 11, "Arkos tick 4, AY regs:");
        for (i = 0; i < 14; i++)
            cwin_putat_printf(&scr, (uint8_t)(i * 3), 12, "%02x", shadow[i]);

        cwin_putat_string(&scr, 0, 14, "Press any key to exit");
        keyb_getch();
        return 0;
    }
    else
        cwin_putat_string(&scr, 0, 8, "Arkos: no tune loaded");

    cwin_putat_string(&scr, 0, 9, "Press any key to exit");
    keyb_getch();

    return 0;
}
