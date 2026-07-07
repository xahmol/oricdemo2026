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
    -t "$SANDBOX/$TAPFILE" -f --loci \
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
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
