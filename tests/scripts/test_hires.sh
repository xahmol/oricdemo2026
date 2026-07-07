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
RUN_CYCLES=16000000

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

    # hb_ellipse_fill test: ellipse centre (60,65) rx=6 ry=3 -- centre row
    # (y=65) column-byte 10 (x=60-65) falls entirely within the x=54-66
    # span, giving the canonical all-ink byte 0x7f, at $AA32.
    check_byte "hb_ellipse_fill (\$AA32)" "0xAA32:1" "7f" "$DUMP"

    # hb_put_chars_center test: "AA" centers to x=114 (col-byte 19,
    # byte-aligned) at row 70 -- row0 of 'A' is the same verified ROM data
    # (0x08 -> 0x48), at $AB03.
    check_byte "hb_put_chars_center (\$AB03)" "0xAB03:1" "48" "$DUMP"

    # hb_rect_pattern test: 8-row stripe tile (top half ink, bottom half
    # paper) over w=6,h=8 at (0,80) -- rows 80-83 = 0x7f, rows 84-87 = 0x40.
    check_byte "hb_rect_pattern row0 (\$AC80)" "0xAC80:1" "7f" "$DUMP"
    check_byte "hb_rect_pattern row1 (\$ACA8)" "0xACA8:1" "7f" "$DUMP"
    check_byte "hb_rect_pattern row2 (\$ACD0)" "0xACD0:1" "7f" "$DUMP"
    check_byte "hb_rect_pattern row3 (\$ACF8)" "0xACF8:1" "7f" "$DUMP"
    check_byte "hb_rect_pattern row4 (\$AD20)" "0xAD20:1" "40" "$DUMP"
    check_byte "hb_rect_pattern row5 (\$AD48)" "0xAD48:1" "40" "$DUMP"
    check_byte "hb_rect_pattern row6 (\$AD70)" "0xAD70:1" "40" "$DUMP"
    check_byte "hb_rect_pattern row7 (\$AD98)" "0xAD98:1" "40" "$DUMP"

    # fixedmath test: oric_sin(64) and oric_cos(0) both hit the table's peak
    # value (127 = 0x7f) at scratch offset $1000/$1001 ($B000/$B001).
    check_byte "oric_sin(64) (\$B000)" "0xB000:1" "7f" "$DUMP"
    check_byte "oric_cos(0) (\$B001)"  "0xB001:1" "7f" "$DUMP"

    # hb_flood_fill containment test, row 95: region A (col-byte 0, x=0-5)
    # filled by the flood -> 0x7f; wall (col-byte 1, x=6-11) unchanged ink
    # -> 0x7f; region B (col-byte 2, x=12-17), across the wall -> still
    # 0x40 (paper), proving the fill did not leak past the wall.
    check_byte "hb_flood_fill region A (\$AED8)" "0xAED8:1" "7f" "$DUMP"
    check_byte "hb_flood_fill wall (\$AED9)"     "0xAED9:1" "7f" "$DUMP"
    check_byte "hb_flood_fill region B (\$AEDA)" "0xAEDA:1" "40" "$DUMP"

    # hb_scroll_up test (sub-canvas rows 130-134): row130 gets row131's
    # content (0x60) shifted up; row134 (vacated) gets the fill (0x40).
    check_byte "hb_scroll_up shifted (\$B450)" "0xB450:1" "60" "$DUMP"
    check_byte "hb_scroll_up vacated (\$B4F0)" "0xB4F0:1" "40" "$DUMP"

    # hb_scroll_down test (sub-canvas rows 140-144): row144 gets row143's
    # content (0x60) shifted down; row140 (vacated) gets the fill (0x40).
    check_byte "hb_scroll_down shifted (\$B680)" "0xB680:1" "60" "$DUMP"
    check_byte "hb_scroll_down vacated (\$B5E0)" "0xB5E0:1" "40" "$DUMP"

    # hb_scroll_left test, row 150: exact 6px (1 column-byte) shift moves
    # col-byte1 (0x60) into col-byte0, and col-byte2 (precleared paper,
    # 0x40) into col-byte1.
    check_byte "hb_scroll_left col0 (\$B770)" "0xB770:1" "60" "$DUMP"
    check_byte "hb_scroll_left col1 (\$B771)" "0xB771:1" "40" "$DUMP"

    # hb_scroll_right test, row 160: pixel at x=5 (col-byte0) moves to
    # x=11 (col-byte1); vacated col-byte0 becomes the fill (paper, 0x40).
    check_byte "hb_scroll_right col0 (\$B900)" "0xB900:1" "40" "$DUMP"
    check_byte "hb_scroll_right col1 (\$B901)" "0xB901:1" "41" "$DUMP"

    # Sprite test: 6x2 all-ink sprite drawn over a paper background at
    # (0,170) -- snapshot right after hspr_draw() shows the sprite (0x7f),
    # taken at scratch $B002/$B003; the final state (after hspr_erase())
    # at the real screen address $BA90/$BAB8 should be back to paper
    # (0x40), proving the backed-up background was restored exactly.
    check_byte "hspr_draw row0 (\$B002)"  "0xB002:1" "7f" "$DUMP"
    check_byte "hspr_draw row1 (\$B003)"  "0xB003:1" "7f" "$DUMP"
    check_byte "hspr_erase row0 (\$BA90)" "0xBA90:1" "40" "$DUMP"
    check_byte "hspr_erase row1 (\$BAB8)" "0xBAB8:1" "40" "$DUMP"

    # hires_row_colors_range test: rows 175/177/179 (stride 2 from 175)
    # get INK=RED(01)/PAPER=GREEN(12); rows 176/178 keep their 0xAA
    # sentinel, proving the stride actually skipped them.
    check_byte "hires_row_colors_range row175 ink (\$BB58)"   "0xBB58:1" "01" "$DUMP"
    check_byte "hires_row_colors_range row175 paper (\$BB59)" "0xBB59:1" "12" "$DUMP"
    check_byte "hires_row_colors_range row176 skipped (\$BB80)" "0xBB80:1" "aa" "$DUMP"
    check_byte "hires_row_colors_range row177 ink (\$BBA8)"   "0xBBA8:1" "01" "$DUMP"
    check_byte "hires_row_colors_range row177 paper (\$BBA9)" "0xBBA9:1" "12" "$DUMP"
    check_byte "hires_row_colors_range row178 skipped (\$BBD0)" "0xBBD0:1" "aa" "$DUMP"
    check_byte "hires_row_colors_range row179 ink (\$BBF8)"   "0xBBF8:1" "01" "$DUMP"
    check_byte "hires_row_colors_range row179 paper (\$BBF9)" "0xBBF9:1" "12" "$DUMP"

    # hires_dissolve_* test: seed=12345's first in-range LFSR output decodes
    # to y=183,x=140 (col-byte 23) -- dissolve_step(true) should set it to
    # 0x48 (bit6 | mask 0x08 for x%6==2), at $BCAF.
    check_byte "hires_dissolve_step (\$BCAF)" "0xBCAF:1" "48" "$DUMP"

    # rasterirq test: marker byte at $B004 (HIRESVRAM+0x1004) should be
    # 0x99, proving hrirq_start() actually enabled interrupts, Timer 1's
    # already free-running 100Hz IRQ fired the installed handler at least
    # once during the busy-wait, and the handler correctly dispatched the
    # registered __interrupt callback.
    check_byte "hrirq callback fired (\$B004)" "0xB004:1" "99" "$DUMP"
fi

echo ""
echo "==========================================================="
echo "  Results: $pass passed, $fail failed"
echo "==========================================================="

if [ $fail -gt 0 ]; then
    exit 1
fi
exit 0
