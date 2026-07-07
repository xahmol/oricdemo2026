// hires_test.c - HIRES build-chain smoke test / library test fixture
//
// Built with -rt=include/oric_crt_hires.c (not the default oric_crt.c).
// Exercises the HIRES library incrementally as it's built out; see
// tests/scripts/test_hires.sh for what's asserted at each stage.

#include "oric.h"
#include "hires.h"
#include "ttf.h"
#include "ttf_test_font.h"   // tests/fixtures/, added to the include path via -i (see Makefile)

int main(void)
{
    // Region-layout smoke test: confirm the far end of the HIRES bitmap
    // address range is writable RAM (not overlapping code/data/stack placed
    // by the linker under oric_crt_hires.c's shrunk 'main'/'stack' regions).
    // Uses $BF00 (not the very last byte, $BF3F) -- that's exercised by the
    // mode-switch test below instead.
    *(volatile uint8_t *)(HIRESVRAM + 0x1F00) = 0x40;

    hires_init();

    HiresBitmap hb;
    hb_init(&hb, (uint8_t *)HIRESVRAM, HIRES_ROWS);

    // Real Oric RAM is NOT zero-initialized at power-on (Phosphoric emulates
    // this realistically) -- hb_set/hb_clr only ever touch bit6 + the one
    // target pixel bit, by design (see hires.c), so any OTHER pixel bits
    // already in a byte legitimately persist across calls. Clear the two
    // bytes under test to a known state (0x40 = bit6 set, all pixels off)
    // before testing, exactly as a real program must before drawing.
    *(volatile uint8_t *)HIRESVRAM       = 0x40;
    *(volatile uint8_t *)(HIRESVRAM + 1) = 0x40;

    // Addressing-table test: x=0 and x=5 both map to column-byte 0 (mask
    // bits 0x20 and 0x01), x=6 maps to column-byte 1 -- confirms
    // hires_col_byte[]/hires_col_mask[] divide/mod-by-6 correctly.
    hb_set(&hb, 0, 0);
    hb_set(&hb, 5, 0);   // byte at $A000 should now be 0x61 (bit6 | bit5 | bit0)
    hb_set(&hb, 6, 0);   // byte at $A001 should now be 0x60 (bit6 | bit5)
    hb_clr(&hb, 0, 0);   // byte at $A000 should now be 0x41 (bit6 | bit0)

    // Row-offset test: a full 6-pixel run on row 1 should produce the
    // canonical "all-ink" byte 0x7F at hires_row_off[1] == $A028.
    hbu_line(&hb, 0, 1, 5, 1, true);

    // Mode-switch test: hires_on(true) should write A_HIRES_50HZ (30) at
    // TEXTVRAM+39 ($BBA7); hires_footer_enable(true) should write
    // A_TEXT_50HZ (26) at the last HIRES byte ($BF3F). hires_off()/
    // hires_footer_disable() aren't separately checked here -- they share
    // the exact same address expressions with a different literal value,
    // already proven correct by these two calls.
    hires_on(true);
    hires_footer_enable(true);

    // Attribute/colour test: row 2's column-bytes 0/1 should hold INK=RED
    // (1) and PAPER=GREEN (18) at hires_row_off[2] == $A050/$A051.
    hires_row_colors(2, A_FWRED, A_BGGREEN);

    // Invert-bit test: hb_set at row 3 col-byte 2 (x=12) gives byte 0x60
    // (bit6|bit5) at hires_row_off[3]+2 == $A07A; hires_invert_byte(on)
    // should then set bit7 too, giving 0xE0. hires_invert_byte(off) isn't
    // separately checked here -- same shared address, different bit op.
    // Cleared first, same reason as the $A000/$A001 clear above.
    *(volatile uint8_t *)(HIRESVRAM + 0x7A) = 0x40;
    hb_set(&hb, 12, 3);
    hires_invert_byte(&hb, 2, 3, true);

    // AIC test: rows 10 (even) and 11 (odd) should get different ink/paper
    // pairs. Row 10 -> ink=WHITE(7)/paper=BGBLACK(16) at $A190/$A191.
    // Row 11 -> ink=CYAN(6)/paper=BGMAGENTA(21) at $A1B8/$A1B9.
    HiresAIC aic;
    hires_aic_init(&aic, A_FWWHITE, A_BGBLACK, A_FWCYAN, A_BGMAGENTA);
    hires_aic_apply_range(&aic, 10, 11);

    // hb_rect_fill test: fill a 6x1 rect at (x=18,y=20) -- exactly column-byte
    // 3 of row 20 (18/6=3) -- should become the canonical all-ink byte 0x7F
    // at hires_row_off[20]+3 == $A323.
    *(volatile uint8_t *)(HIRESVRAM + 0x323) = 0x40;
    hb_rect_fill(&hb, (const HiresClip *)0, 18, 20, 6, 1, true);

    // hb_circle_fill test: circle centre (30,25) r=3. At the centre row
    // (y=25), the span is x=27..33 (dx grows to 3 for dy=0) -- column-byte 5
    // (x=30-35) gets pixels 30-33 set, 34-35 not -> byte 0x7c, at
    // hires_row_off[25]+5 == $A3ED.
    *(volatile uint8_t *)(HIRESVRAM + 0x3ED) = 0x40;
    hb_circle_fill(&hb, (const HiresClip *)0, 30, 25, 3, true);

    // hb_triangle_fill test: triangle (10,30)-(20,30)-(15,35). At row 32,
    // the even-odd edge test gives an inside span of exactly x=12..17 --
    // all 6 pixels of column-byte 2 -- so that byte becomes the canonical
    // all-ink 0x7F, at hires_row_off[32]+2 == $A502 (hand-verified when
    // this test was written -- see task notes).
    *(volatile uint8_t *)(HIRESVRAM + 0x502) = 0x40;
    hb_triangle_fill(&hb, (const HiresClip *)0, 10, 30, 20, 30, 15, 35, true);

    // hb_bitblit test: build a source byte at row 34 col-byte 2 (x=12-17)
    // with bits at x=12 and x=14 set (0x40|0x20|0x08=0x68), then copy 6x1
    // pixels to row 36 col-byte 2 -- destination should end up 0x68 too, at
    // hires_row_off[36]+2 == $A5A2.
    *(volatile uint8_t *)(HIRESVRAM + 0x552) = 0x40;   // row 34 col-byte 2
    *(volatile uint8_t *)(HIRESVRAM + 0x5A2) = 0x40;   // row 36 col-byte 2
    hb_set(&hb, 12, 34);
    hb_set(&hb, 14, 34);
    hb_bitblit(&hb, (const HiresClip *)0, 12, 36, &hb, 12, 34, 6, 1, HBLIT_COPY);

    // hb_put_chars test: render "A" at (0,40) using the ROM charset --
    // tests/scripts/test_hires.sh checks the 8 resulting rows against the
    // ROM glyph bytes (read directly from CHARSETROM, OR'd with 0x40) so
    // this doesn't depend on guessing the ROM's exact glyph bitmap. Rows
    // 40-47 col-byte 0 cleared first, same reason as earlier clears above.
    for (uint8_t row = 0; row < 8; row++)
        *(volatile uint8_t *)(HIRESVRAM + (uint16_t)(40 + row) * HIRES_ROW_BYTES) = 0x40;
    hb_put_chars(&hb, (const HiresClip *)0, 0, 40, "A", 1);

    // ttf_print test: render "!" (code 33) at (0,50) using the checked-in
    // testfont fixture (Go-Mono-Bold @ 8pt, width_bytes=1 so this glyph's
    // whole row is 1 byte) -- expected bytes are that glyph's own data
    // (tests/fixtures/ttf_test_font.h) OR'd with 0x40, since width=5 means
    // only pixel columns 0-4 (mask bits 0x20-0x02) are touched, leaving
    // bit0 (mask 0x01) as whatever the pre-clear left it (0). Rows 50-59
    // col-byte 0 cleared first, same reason as earlier clears above.
    for (uint8_t row = 0; row < 10; row++)
        *(volatile uint8_t *)(HIRESVRAM + (uint16_t)(50 + row) * HIRES_ROW_BYTES) = 0x40;
    ttf_print(&hb, (const HiresClip *)0, &testfont, 0, 50, "!");

    for (;;)
        ;

    return 0;
}
