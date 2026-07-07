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

    cwin_putat_string(&scr, 0, 9, "Press any key to exit");
    keyb_getch();

    return 0;
}
