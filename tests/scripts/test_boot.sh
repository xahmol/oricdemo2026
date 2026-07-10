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
    # PT3 decoder test: tests/fixtures/music.pt3 (a small synthetic module,
    # see src/main.c's comment) is loaded via LOCI (--loci-flash mounts
    # tests/sandbox, where sandbox-reset copies it) and pt3_init()+one
    # pt3_tick() computes these exact 14 AY register values -- hand-verified
    # against the module's own hand-crafted commands (volume 15/ornament+
    # sample 0/note 0 on channel A, volume 10/note 12 on channel B, release
    # on channel C) before being locked in here. This is real behavioral
    # verification of the decode path, not just "didn't crash".
    #
    # Byte 7 (mixer, 0x24) reflects a real fix: the tone/noise mixer-enable
    # bits are now derived from the CURRENT sample step's own flags byte
    # (bit4=tone mask, bit7=noise mask, both active-low) every tick, per
    # ppt3.s's CH_MIX/CH_EXIT -- not a permanent per-channel latch that,
    # once set by the unrelated noise-period-select command, never turned
    # back off (a real, confirmed bug -- see project memory
    # project_pt3_sample_select_bug's RESOLUTION section). 0x24 vs the old
    # 0x3C differs in exactly the noise-A/noise-B bits, matching sample 0's
    # own step-0 flags wanting noise on for those channels.
    #
    # Byte 9 (channel B's volume register, 0x0B) reflects a second real
    # fix: PT3_VOLUME_TABLE (ppt3.s's own VolTableCreator table, precisely
    # re-derived) replaces an earlier linear volume*amplitude combine that
    # measurably under-represented most combinations -- channel B's own
    # volume=10 combined with sample 0's own amplitude nibble now correctly
    # resolves to 11 (0x0B), not 10 (0x0A), via the real table.
    #
    # tests/fixtures/music.pt3's sample 0 step 0 was patched (2 bytes
    # swapped) for a third, separate real fix: fetching ppt3.s directly and
    # tracing CH_NOAM shows the amplitude nibble added to CrAmSl comes from
    # the sample step's SECOND byte (z80_B, this project's sam_mixflags),
    # not its first (z80_C/sam_flags) -- see pt3_channel_tick()'s own
    # comment and docs/pt3.md. The fixture originally put its test
    # amplitude (0x0F) in the flags byte (matching the old, wrong read);
    # swapping it into mixflags keeps this assertion exercising the same
    # volume-table combine (15,15 -> 15 and 10,15 -> 11) under the
    # corrected byte source, rather than silently degrading to amplitude=0.
    check_found "PT3 tune loaded"       "PT3 tune loaded, AY regs:"    "$BOOT_DUMP"
    check_found "PT3 AY registers"      "79 07 BD 03 00 00 00 24 0F 0B 00 00 00" "$BOOT_DUMP"
    # PT3 effects test: tests/fixtures/music_effects.pt3 exercises
    # portamento (channel A slides note 0 -> 12, delay=2/step=50, over 5
    # ticks: 1913 -> 1913 -> 1863 -> 1863 -> 1813), vibrato (channel B,
    # on=2/off=3 duration pulsing -- audible again by tick 5), and
    # envelope-glide (delay=2/step=+10, sweeping the shared envelope
    # period to 20 by tick 5) -- hand-computed tick-by-tick before being
    # locked in, see docs/pt3.md's Verification section for the full trace.
    # Bytes 7/9 updated 0x3C->0x24 and 0x0A->0x0B for the same two reasons
    # as above.
    check_found "PT3 effects tick 5"    "PT3 effects tick 5, AY regs:" "$BOOT_DUMP"
    check_found "PT3 effects AY registers" "15 07 FD 04 00 00 00 24 0F 0B 00 14 00" "$BOOT_DUMP"
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
