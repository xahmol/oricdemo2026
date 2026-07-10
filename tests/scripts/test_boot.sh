#!/usr/bin/env bash
# tests/scripts/test_boot.sh
#
# Boot smoke test (the `make test-boot` / `make test` target).
#
# Fast-loads the freshly built oricdemo.tap under Atmos BASIC 1.1 in
# Phosphoric and decodes the $BB80 screen-text dump to assert the status
# lines from src/main.c render correctly.
#
# Required env vars (set by `make test-boot`):
#   PHOS      path to oric1-emu
#   ATMOSROM  path to roms/basic11b.rom
#   SANDBOX   tests/sandbox (freshly built .tap)
#   OUT       tests/out (scratch dir for RAM dumps)
#   TAPFILE   oricdemo.tap

set -u
cd "$(dirname "$0")/../.." || exit 1

SCREEN=tests/scripts/oric_screen.py

# Calibrated cycle count -- long enough for main() to run past
# charwin_init()/ijk_detect()/loci_present() and draw the status screen.
BOOT_CYCLES=8000000

BOOT_DUMP="$OUT/capture_boot.bin"

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
echo "  oricdemo2026 -- boot smoke test"
echo "==========================================================="

if [ ! -x "$PHOS" ]; then
    echo "  oric1-emu not found/executable at $PHOS -- skipping"
    exit 0
fi

"$PHOS" -r "$ATMOSROM" \
    -t "$SANDBOX/$TAPFILE" -f --loci-flash "$SANDBOX" \
    --headless -c $BOOT_CYCLES \
    --dump-ram-at $BOOT_CYCLES:"$BOOT_DUMP" >/dev/null 2>&1

if [ ! -f "$BOOT_DUMP" ]; then
    echo "  [FAIL] emulator did not produce expected RAM dump"
    fail=$((fail+1))
else
    echo ""
    check_found "title renders"       "ORIC DEMO 2026"          "$BOOT_DUMP"
    check_found "build-chain OK line" "Oscar64 build chain OK"  "$BOOT_DUMP"
    check_found "exit prompt renders" "Press any key to exit"   "$BOOT_DUMP"
    # Arkos decoder test: tests/fixtures/arkos_test.aky (a tiny synthetic
    # module, 35 bytes, see src/buildtest.c's own comment) is loaded via
    # LOCI (--loci-flash mounts tests/sandbox, where sandbox-reset copies
    # it) and arkos_init()+one arkos_tick() computes these exact 14 AY
    # register values -- hand-verified against a from-scratch Python decode
    # replica of akyplayer.s (same methodology as the PT3 player's own
    # verification), not just "didn't crash". Registers 0-5 (tone periods)
    # and 11-13 (hardware envelope) stay 0 -- this fixture's byte pattern
    # (NoSoftNoHard/NoSoftNoHard-or-loop paths only) never touches them.
    # Register 6 (noise period) also stays 0 -- no noise bit ever set.
    # Register 7 (mixer) is 0x1F: all 3 channels' NoSoftNoHard/-or-loop
    # decode calls arkos_rb_close_tone() once each, shifted/accumulated
    # into 0xE0 -> 0x1F across the 3-channel loop. Registers 8/9/10
    # (volume A/B/C) are all 0x40 -- all 3 channels decode the SAME shared
    # RegisterBlock byte (0x01, a real track/registerblock-reuse scenario,
    # not a fixture shortcut -- see docs/arkos.md), so all 3 channels
    # compute the identical volume value.
    check_found "Arkos tune loaded"     "Arkos tune loaded, AY regs:"  "$BOOT_DUMP"
    check_found "Arkos AY registers"    "00 00 00 00 00 00 00 1F 40 40 40 00 00" "$BOOT_DUMP"
    # Arkos tick 4: 3 more NON-INITIAL frames (bytes 0x00, 0x04, 0x0C, see
    # src/buildtest.c's own comment) continuing from wherever the previous
    # frame's cursor left off within the SAME Track triple (duration 4) --
    # exercises the NoSoftNoHard-or-loop dispatch's masked-value branching
    # (0, 1, 3) without hitting the loop case itself (masked==2, exercised
    # instead at the Linker level by this fixture's own end-of-song entry).
    # Mixer stays 0x1F (same close_tone() accumulation each frame); volume
    # A/B/C become 0x01 (byte 0x0C's own computed value, same for all 3
    # channels since they're still decoding the same shared RegisterBlock).
    check_found "Arkos tick 4"          "Arkos tick 4, AY regs:"       "$BOOT_DUMP"
    check_found "Arkos tick 4 AY registers" "00 00 00 00 00 00 00 1F 01 01 01 00 00" "$BOOT_DUMP"
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
