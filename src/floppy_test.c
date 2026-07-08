// floppy_test.c - floppy-disk build target entry point (see docs/floppy.md)
//
// Build-chain smoke test for the floppy-disk target, analogous to
// src/main.c (tape/LOCI target) and src/hires_test.c (HIRES target): not
// demo content, proves the runtime (include/oric_crt_floppy.c), the
// resident loader (tools/floppy/loader.c), and include/floppy.c's
// floppy_load() work end-to-end, exercised by `make test-disk` (see
// tests/scripts/test_disk.sh).
//
// This program IS the thing tools/floppy/loader.c's boot handoff jumps
// to -- it is compiled and embedded into the disk image as one of
// tools/oric_floppybuilder.py's AddFile entries, at a track/sector the
// loader's own DEMO_TRACK/DEMO_SECTOR constants must match (see the
// Makefile's two-pass build, docs/floppy.md).
//
// File-index convention for THIS test (coordinated with
// tests/scripts/test_disk.sh's own FloppyBuilder script -- these are
// fixed, known indices for testing only, not a general convention):
//   index 0 = this program itself (the boot-handoff target -- never
//             loaded via floppy_load(), only via the boot sector+loader)
//   index 1 = a small raw test payload, used to exercise floppy_load()
//             directly (see PAYLOAD_TEST_SIZE below)
//   index 2 = tests/fixtures/music.pt3, exercised via pt3_load()'s
//             STORAGE_FLOPPY (compile-time index) overload

#include "oric.h"
#include "charwin.h"
#include "keyboard.h"
#include "floppy.h"
#include "pt3.h"

// Must match whatever tests/scripts/test_disk.sh's FloppyBuilder script
// places at file index 1 -- see that script for the actual bytes.
#define PAYLOAD_TEST_SIZE 64

int main(void)
{
    static uint8_t payload[PAYLOAD_TEST_SIZE];
    int16_t r;
    OricCharWin scr;

    charwin_init();

    cwin_init(&scr, 2, 0, 38, 28, A_FWWHITE, A_BGBLACK);
    cwin_clear(&scr);

    cwin_putat_string(&scr, 0, 2, "ORIC DEMO 2026 - FLOPPY BUILD");
    cwin_putat_string(&scr, 0, 4, "Floppy runtime + loader OK");

    r = floppy_load(1, payload, PAYLOAD_TEST_SIZE);
    if (r == PAYLOAD_TEST_SIZE)
        cwin_putat_string(&scr, 0, 6, "floppy_load: payload OK");
    else
        cwin_putat_printf(&scr, 0, 6, "floppy_load: FAILED (r=%d)", r);

    // PT3 player smoke test via the floppy backend -- same fixture and
    // same assertion shape as src/main.c's own LOCI-backed test (see that
    // file's comment for what tests/fixtures/music.pt3 actually contains),
    // just loaded by compile-time file index instead of a path string.
    if (pt3_load(2))
    {
        const uint8_t *shadow;
        uint8_t i;
        pt3_init();
        pt3_tick();
        shadow = pt3_debug_shadow();
        cwin_putat_string(&scr, 0, 8, "PT3 tune loaded, AY regs:");
        for (i = 0; i < 14; i++)
            cwin_putat_printf(&scr, (uint8_t)(i * 3), 9, "%02x", shadow[i]);
    }
    else
        cwin_putat_string(&scr, 0, 8, "PT3: no tune loaded");

    cwin_putat_string(&scr, 0, 11, "Press any key to exit");
    keyb_getch();

    return 0;
}
