#!/usr/bin/env bash
# tests/scripts/test_hires.sh
#
# HIRES library smoke test (the `make test-hires` target).
#
# Fast-loads the freshly built hires_test.tap (built with the alternate
# include/oric_crt_hires.c runtime) under Atmos BASIC 1.1 in Phosphoric and
# hex-dumps specific HIRES-bitmap addresses via oric_screen.py's --bytes
# flag, asserting exact byte values -- this is a region-layout/hardware
# smoke test, not a text-screen check, so oric_screen.py's --find (text
# search) isn't used here.
#
# Required env vars (set by `make test-hires`):
#   PHOS      path to oric1-emu
#   ATMOSROM  path to roms/basic11b.rom
#   SANDBOX   tests/sandbox (freshly built .tap)
#   OUT       tests/out (scratch dir for RAM dumps)
#   TAPFILE   hires_test.tap

set -u
cd "$(dirname "$0")/../.." || exit 1

SCREEN=tests/scripts/oric_screen.py

# Cycle count -- must be large enough for Phosphoric's fast-load (tape
# search + BASIC boot) to finish before the program itself runs, same
# ballpark as test_boot.sh, even though this program's own logic is trivial.
RUN_CYCLES=8000000

DUMP="$OUT/capture_hires.bin"

pass=0
fail=0

check_byte() {
    local label="$1" addr_len="$2" expected="$3" dump="$4"
    local actual
    actual=$(python3 "$SCREEN" "$dump" --bytes "$addr_len" 2>/dev/null)
    if [ "$actual" = "$expected" ]; then
        echo "  [PASS] $label ($addr_len = $actual)"
        pass=$((pass+1))
    else
        echo "  [FAIL] $label -- expected '$expected', got '$actual'"
        fail=$((fail+1))
    fi
}

echo "==========================================================="
echo "  oricdemo2026 -- HIRES region-layout smoke test"
echo "==========================================================="

if [ ! -x "$PHOS" ]; then
    echo "  oric1-emu not found/executable at $PHOS -- skipping"
    exit 0
fi

"$PHOS" -r "$ATMOSROM" \
    -t "$SANDBOX/$TAPFILE" -f --loci \
    --headless -c $RUN_CYCLES \
    --dump-ram-at $RUN_CYCLES:"$DUMP" >/dev/null 2>&1

if [ ! -f "$DUMP" ]; then
    echo "  [FAIL] emulator did not produce expected RAM dump"
    fail=$((fail+1))
else
    echo ""
    # Region-layout: far end of the HIRES bitmap ($BF00) poked to 0x40.
    check_byte "HIRESVRAM region layout (\$BF00)" "0xBF00:1" "40" "$DUMP"

    # Addressing-table test (src/hires_test.c): hb_set(0,0)+hb_set(5,0) then
    # hb_set(6,0) then hb_clr(0,0) -- confirms x/6 and x%6 addressing.
    check_byte "hb_set/hb_clr col-byte 0 (\$A000)" "0xA000:1" "41" "$DUMP"
    check_byte "hb_set col-byte 1 (\$A001)"        "0xA001:1" "60" "$DUMP"

    # Row-offset test: hbu_line(0,1, 5,1) fills all 6 pixels of row 1's
    # first byte -- the canonical all-ink byte 0x7f, at hires_row_off[1]=$28.
    check_byte "hbu_line row-offset (\$A028)"       "0xA028:1" "7f" "$DUMP"

    # Mode-switch test: hires_on(true) writes A_HIRES_50HZ (30 = 0x1e) at
    # TEXTVRAM+39 ($BBA7); hires_footer_enable(true) writes A_TEXT_50HZ
    # (26 = 0x1a) at the last HIRES byte ($BF3F).
    check_byte "hires_on 50Hz attr (\$BBA7)"        "0xBBA7:1" "1e" "$DUMP"
    check_byte "hires_footer_enable 50Hz attr (\$BF3F)" "0xBF3F:1" "1a" "$DUMP"

    # Attribute/colour test: hires_row_colors(2, A_FWRED, A_BGGREEN) writes
    # INK=1 at col-byte 0, PAPER=18 at col-byte 1, of row 2 (hires_row_off[2]=$50).
    check_byte "hires_row_colors ink (\$A050)"  "0xA050:1" "01" "$DUMP"
    check_byte "hires_row_colors paper (\$A051)" "0xA051:1" "12" "$DUMP"

    # Invert-bit test: hb_set(12,3) then hires_invert_byte(on) at row 3
    # col-byte 2 ($A07A) should give 0xe0 (bit7|bit6|bit5).
    check_byte "hires_invert_byte on (\$A07A)" "0xA07A:1" "e0" "$DUMP"

    # AIC test: row 10 (even) -> ink=WHITE(07)/paper=BGBLACK(10 hex=16dec)
    # at $A190/$A191; row 11 (odd) -> ink=CYAN(06)/paper=BGMAGENTA(15 hex=21dec)
    # at $A1B8/$A1B9.
    check_byte "hires_aic even-row ink (\$A190)"   "0xA190:1" "07" "$DUMP"
    check_byte "hires_aic even-row paper (\$A191)" "0xA191:1" "10" "$DUMP"
    check_byte "hires_aic odd-row ink (\$A1B8)"    "0xA1B8:1" "06" "$DUMP"
    check_byte "hires_aic odd-row paper (\$A1B9)"  "0xA1B9:1" "15" "$DUMP"

    # hb_rect_fill test: 6x1 rect at (18,20) fills column-byte 3 of row 20
    # entirely -- canonical all-ink byte 0x7f, at $A323.
    check_byte "hb_rect_fill (\$A323)" "0xA323:1" "7f" "$DUMP"

    # hb_circle_fill test: circle centre (30,25) r=3 -- centre row spans
    # x=27-33, so column-byte 5 (x=30-35) gets bits for x=30-33 set, giving
    # 0x7c, at $A3ED.
    check_byte "hb_circle_fill (\$A3ED)" "0xA3ED:1" "7c" "$DUMP"

    # hb_triangle_fill test: triangle (10,30)-(20,30)-(15,35) -- at row 32
    # the inside span is exactly x=12-17 (column-byte 2, hand-verified),
    # giving the canonical all-ink byte 0x7f, at $A502.
    check_byte "hb_triangle_fill (\$A502)" "0xA502:1" "7f" "$DUMP"

    # hb_bitblit test: source byte 0x68 (bits at x=12,14) at row 34 col-byte
    # 2 ($A552), copied to row 36 col-byte 2 ($A5A2) -- destination should
    # match the source exactly.
    check_byte "hb_bitblit source (\$A552)"      "0xA552:1" "68" "$DUMP"
    check_byte "hb_bitblit destination (\$A5A2)" "0xA5A2:1" "68" "$DUMP"

    # hb_put_chars test: "A" rendered at (0,40) via the ROM charset --
    # expected bytes are the ROM glyph for 'A' (CHARSETROM+0x108, read
    # directly from this same dump at $FD80-$FD87) OR'd with 0x40.
    check_byte "hb_put_chars row0 (\$A640)" "0xA640:1" "48" "$DUMP"
    check_byte "hb_put_chars row1 (\$A668)" "0xA668:1" "54" "$DUMP"
    check_byte "hb_put_chars row2 (\$A690)" "0xA690:1" "62" "$DUMP"
    check_byte "hb_put_chars row3 (\$A6B8)" "0xA6B8:1" "62" "$DUMP"
    check_byte "hb_put_chars row4 (\$A6E0)" "0xA6E0:1" "7e" "$DUMP"
    check_byte "hb_put_chars row5 (\$A708)" "0xA708:1" "62" "$DUMP"
    check_byte "hb_put_chars row6 (\$A730)" "0xA730:1" "62" "$DUMP"
    check_byte "hb_put_chars row7 (\$A758)" "0xA758:1" "40" "$DUMP"

    # ttf_print test: "!" rendered at (0,50) via the checked-in testfont
    # fixture -- expected bytes are that glyph's own data (tests/fixtures/
    # ttf_test_font.h, offset 10, 1 byte/row) OR'd with 0x40.
    check_byte "ttf_print row0 (\$A7D0)" "0xA7D0:1" "40" "$DUMP"
    check_byte "ttf_print row1 (\$A7F8)" "0xA7F8:1" "48" "$DUMP"
    check_byte "ttf_print row2 (\$A820)" "0xA820:1" "48" "$DUMP"
    check_byte "ttf_print row3 (\$A848)" "0xA848:1" "48" "$DUMP"
    check_byte "ttf_print row4 (\$A870)" "0xA870:1" "48" "$DUMP"
    check_byte "ttf_print row5 (\$A898)" "0xA898:1" "48" "$DUMP"
    check_byte "ttf_print row6 (\$A8C0)" "0xA8C0:1" "40" "$DUMP"
    check_byte "ttf_print row7 (\$A8E8)" "0xA8E8:1" "48" "$DUMP"
    check_byte "ttf_print row8 (\$A910)" "0xA910:1" "40" "$DUMP"
    check_byte "ttf_print row9 (\$A938)" "0xA938:1" "40" "$DUMP"
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
