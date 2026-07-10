#!/usr/bin/env bash
# tests/scripts/test_disk.sh
#
# Floppy-disk boot smoke test (the `make test-disk` target -- see
# docs/floppy.md). Boots build/oricdemo_floppy.dsk under Phosphoric's
# Microdisc emulation (no ROM, no LOCI, no tape -- the disk's own boot
# sector + resident loader do everything) and decodes the $BB80 screen-text
# dump to assert src/floppy_test.c's status lines render correctly.
#
# Required env vars (set by `make test-disk`):
#   PHOS      path to oric1-emu
#   ATMOSROM  path to roms/basic11b.rom (Phosphoric always needs a base ROM
#             image to start from, even though this target boots entirely
#             off the disk once running -- see docs/floppy.md)
#   DISKROM   path to roms/microdis.rom (Microdisc boot EPROM)
#   DSKFILE   build/oricdemo_floppy.dsk
#   OUT       tests/out (scratch dir for RAM dumps)

set -u
cd "$(dirname "$0")/../.." || exit 1

SCREEN=tests/scripts/oric_screen.py

# Calibrated cycle count -- long enough for the boot sector to relocate
# itself, the resident loader to read and jump to the demo binary, and
# main() to run past charwin_init()/floppy_load()/arkos_load() and draw
# every status line (see src/floppy_test.c). Verified stable well past this
# point too (PC settles into keyb_getch()'s own poll loop, correctly
# waiting for a key that never comes in headless mode).
BOOT_CYCLES=25000000

BOOT_DUMP="$OUT/capture_disk.bin"

pass=0
fail=0

check_found() {
    local label="$1" needle="$2" dump="$3"
    if python3 "$SCREEN" "$dump" --find "$needle" >/dev/null 2>&1; then
        echo "  [PASS] $label"
        pass=$((pass+1))
    else
        echo "  [FAIL] $label -- '$needle' not found"
        fail=$((fail+1))
    fi
}

echo "==========================================================="
echo "  oricdemo2026 -- floppy-disk boot smoke test"
echo "==========================================================="

if [ ! -x "$PHOS" ]; then
    echo "  oric1-emu not found/executable at $PHOS -- skipping"
    exit 0
fi

if [ ! -f "$DISKROM" ]; then
    echo "  Microdisc ROM not found at $DISKROM -- skipping"
    exit 0
fi

"$PHOS" -r "$ATMOSROM" \
    -d "$DSKFILE" --disk-rom "$DISKROM" \
    --headless -c $BOOT_CYCLES \
    --dump-ram-at $BOOT_CYCLES:"$BOOT_DUMP" >/dev/null 2>&1

if [ ! -f "$BOOT_DUMP" ]; then
    echo "  [FAIL] emulator did not produce expected RAM dump"
    fail=$((fail+1))
else
    echo ""
    check_found "title renders"            "ORIC DEMO 2026 - FLOPPY BUILD"  "$BOOT_DUMP"
    check_found "runtime+loader OK line"    "Floppy runtime + loader OK"     "$BOOT_DUMP"
    # floppy_load() smoke test: tests/fixtures/floppy_payload_test.bin (64
    # bytes, 0x00-0x3F) loaded via file index 1 -- proves the resident
    # loader's LoadData actually moves real bytes through the fixed API
    # trampoline at $FFEF-$FFF9, not just "didn't crash". Also the
    # regression case for a real bug found and fixed during development:
    # LoadData used to always store a full 256-byte sector regardless of
    # the requested size, silently overflowing this 64-byte buffer by 192
    # bytes into whatever memory followed it (see loader.c's fetch_byte
    # comment and docs/floppy.md).
    check_found "floppy_load payload OK"    "floppy_load: payload OK"        "$BOOT_DUMP"
    # Arkos smoke test via the STORAGE_FLOPPY arkos_load(uint8_t file_index)
    # overload (tests/fixtures/arkos_test.aky, file index 2, same fixture as
    # test_boot.sh's own LOCI-backed check) -- arkos_init()+one arkos_tick()
    # computes these AY register values. Same expected values as
    # test_boot.sh's own "Arkos AY registers" tick-1 check (see that file's
    # comment for the full byte-by-byte trace) -- this target has no
    # overlay-RAM concept at all, so arkos_load() here is a plain load
    # straight into $C000 (see docs/floppy.md/docs/arkos.md), and the
    # module data is byte-identical to the tape/LOCI target's copy.
    check_found "Arkos tune loaded"         "Arkos tune loaded, AY regs:"    "$BOOT_DUMP"
    check_found "Arkos AY registers"        "00 00 00 00 00 00 00 1F 40 40 40 00 00" "$BOOT_DUMP"
    check_found "exit prompt renders"       "Press any key to exit"         "$BOOT_DUMP"
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
