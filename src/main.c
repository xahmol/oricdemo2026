// main.c - oricdemo2026 entry point
//
// Minimal smoke-test program for the Oscar64/Oric build chain: initialises
// the character-window library, keyboard scanner, IJK joystick probe, and
// LOCI storage probe, then reports status on screen. Replace with actual
// demo effects.

#include "oric.h"
#include "charwin.h"
#include "keyboard.h"
#include "ijk.h"
#include "loci.h"
#include "pt3.h"

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

    // PT3 player smoke test: tests/fixtures/music.pt3 is a small synthetic
    // module (not a real tune) hand-built to exercise the decoder's core
    // path -- volume/ornament/sample-select/note commands on channel A,
    // volume/note on channel B, a release on channel C. One pt3_tick()
    // call (direct, not via hrirq -- this is just testing the decode/
    // register-compute logic, not real-time playback) should compute known
    // AY register values; the shadow array is printed as hex here rather
    // than dumped from a fixed RAM address (this project's default runtime
    // gives static arrays a linker-assigned address, not a hardware-fixed
    // one, so a text-search assertion is the simplest correctness check --
    // see tests/scripts/test_boot.sh and docs/pt3.md for what's verified).
    if (pt3_load("music.pt3"))
    {
        const uint8_t *shadow;
        uint8_t i;
        pt3_init();
        pt3_tick();
        shadow = pt3_debug_shadow();
        cwin_putat_string(&scr, 0, 8, "PT3 tune loaded, AY regs:");
        for (i = 0; i < 14; i++)
            cwin_putat_printf(&scr, (uint8_t)(i * 3), 9, "%02x", shadow[i]);

        // Effects test: tests/fixtures/music_effects.pt3 exercises
        // portamento (channel A, sliding note 0 -> 12), vibrato (channel
        // B, on/off amplitude pulsing), and envelope-glide (a shared
        // sweep triggered by channel A's first row) across 5 ticks --
        // see docs/pt3.md's Verification section for the hand-computed
        // expected values.
        if (pt3_load("music_effects.pt3"))
        {
            pt3_init();
            for (i = 0; i < 5; i++)
                pt3_tick();
            shadow = pt3_debug_shadow();
            cwin_putat_string(&scr, 0, 11, "PT3 effects tick 5, AY regs:");
            for (i = 0; i < 14; i++)
                cwin_putat_printf(&scr, (uint8_t)(i * 3), 12, "%02x", shadow[i]);
        }

        cwin_putat_string(&scr, 0, 14, "Press any key to exit");
        keyb_getch();
        return 0;
    }
    else
        cwin_putat_string(&scr, 0, 8, "PT3: no tune loaded");

    cwin_putat_string(&scr, 0, 9, "Press any key to exit");
    keyb_getch();

    return 0;
}
