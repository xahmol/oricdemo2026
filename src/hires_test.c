// hires_test.c - HIRES build-chain smoke test / library test fixture
//
// Built with -rt=include/oric_crt_hires.c (not the default oric_crt.c).
// Exercises the HIRES library incrementally as it's built out; see
// tests/scripts/test_hires.sh for what's asserted at each stage.

#include "oric.h"
#include "hires.h"
#include "ttf.h"
#include "ttf_test_font.h"   // tests/fixtures/, added to the include path via -i (see Makefile)
#include "fixedmath.h"
#include "sprite.h"
#include "dissolve.h"
#include "rasterirq.h"

// hrirq test callback: writes a marker byte so the RAM dump can prove the
// IRQ handler actually fired and dispatched it. Must be __interrupt (saves
// Oscar64's ZP pseudo-register file) since it's called from within
// _hrirq_handler's __hwinterrupt context -- see rasterirq.h.
__interrupt void hrirq_test_callback(void)
{
    *(volatile uint8_t *)(HIRESVRAM + 0x1004) = 0x99;
}

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

    // hb_ellipse_fill test: ellipse centre (60,65) rx=6 ry=3. At the centre
    // row (dy=0), half-width grows to exactly rx=6, spanning x=54-66 --
    // column-byte 10 (x=60-65) falls entirely within that span, giving the
    // canonical all-ink byte 0x7F, at hires_row_off[65]+10 == $AA32.
    // hb_circle_fill isn't separately checked here -- it's now a thin
    // wrapper over hb_ellipse_fill, already exercised above.
    *(volatile uint8_t *)(HIRESVRAM + 0xA32) = 0x40;
    hb_ellipse_fill(&hb, (const HiresClip *)0, 60, 65, 6, 3, true);

    // hb_put_chars_center test: "AA" (len=2, width=12) at row 70 centers to
    // x=(240-12)/2=114 -- exactly column-byte 19 (byte-aligned, no split).
    // Row 0 of the first 'A' glyph is the same ROM data verified by the
    // earlier hb_put_chars test (0x08 -> 0x48 OR'd with 0x40), now at
    // hires_row_off[70]+19 == $AB03.
    for (uint8_t row = 0; row < 8; row++)
        *(volatile uint8_t *)(HIRESVRAM + (uint16_t)(70 + row) * HIRES_ROW_BYTES + 19) = 0x40;
    hb_put_chars_center(&hb, (const HiresClip *)0, 70, "AA", 2);

    // hb_rect_pattern test: an 8-row tile, top half (rows 0-3) all-ink
    // (0xFF), bottom half (rows 4-7) all-paper (0x00), filled over a
    // w=6,h=8 rect at (0,80) -- since w=6 spans all 6 pixel bits of
    // column-byte 0, every bit is explicitly touched (no pre-clear needed,
    // unlike ORing primitives elsewhere). Expect rows 80-83 = 0x7F,
    // rows 84-87 = 0x40, at hires_row_off[80..87]+0 == $AC80.._AD98.
    {
        static const uint8_t stripe_pattern[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };
        hb_rect_pattern(&hb, (const HiresClip *)0, 0, 80, 6, 8, stripe_pattern);
    }

    // fixedmath test: oric_sin(64) (quarter turn, 90deg) should be the
    // table's peak value 127; oric_cos(0) reuses the same table entry via
    // the +64 phase shift, also 127. Scratch offset $1000 is well clear of
    // every other test's addresses (row ~102, untouched elsewhere).
    *(volatile int8_t *)(HIRESVRAM + 0x1000) = oric_sin(64);
    *(volatile int8_t *)(HIRESVRAM + 0x1001) = oric_cos(0);

    // hb_flood_fill containment test: row 95, clear cols 0-17 (3 column
    // bytes) to paper, then draw a 6px-wide ink "wall" at col-byte 1
    // (x=6-11). Flood-filling from x=2 (region A, col-byte 0) with a
    // top==bottom clip confines the scan to this single row -- it should
    // fill region A up to the wall and stop, leaving region B (col-byte 2,
    // x=12-17, on the far side of the wall) completely untouched --
    // proving the wall actually contains the fill rather than it silently
    // covering the whole row regardless.
    hb_rect_fill(&hb, (const HiresClip *)0, 0, 95, 18, 1, false);
    hb_rect_fill(&hb, (const HiresClip *)0, 6, 95, 6, 1, true);
    {
        HiresClip row95 = { 95, 0, 95, 17 };
        hb_flood_fill(&hb, &row95, 2, 95, true);
    }

    // hb_scroll_up test: a 5-row sub-canvas over rows 130-134 (hb_init lets
    // any address serve as a canvas base, not just HIRESVRAM -- this keeps
    // the scroll test's shifted content confined to its own address range,
    // not disturbing rows used by other tests). Set local row1 (=global
    // row131) col-byte0 distinctively (0x60), scroll up by 1: local row0
    // (=row130) should end up with that same 0x60 (shifted up from row1),
    // and the vacated last row (local row4=row134) should be the fill
    // value 0x40.
    {
        HiresBitmap hb_scroll_up_test;
        hb_init(&hb_scroll_up_test, (uint8_t *)(HIRESVRAM + (uint16_t)130 * HIRES_ROW_BYTES), 5);
        for (uint8_t row = 0; row < 5; row++)
            *(volatile uint8_t *)(HIRESVRAM + (uint16_t)(130 + row) * HIRES_ROW_BYTES) = 0x40;
        hb_set(&hb_scroll_up_test, 0, 1);
        hb_scroll_up(&hb_scroll_up_test, 1, 0x40);
    }

    // hb_scroll_down test: a 5-row sub-canvas over rows 140-144. Set local
    // row3 (=row143) col-byte0 to 0x60, scroll down by 1: local row4
    // (=row144) should end up with that 0x60 (shifted down from row3), and
    // the vacated first row (local row0=row140) should be the fill 0x40.
    {
        HiresBitmap hb_scroll_down_test;
        hb_init(&hb_scroll_down_test, (uint8_t *)(HIRESVRAM + (uint16_t)140 * HIRES_ROW_BYTES), 5);
        for (uint8_t row = 0; row < 5; row++)
            *(volatile uint8_t *)(HIRESVRAM + (uint16_t)(140 + row) * HIRES_ROW_BYTES) = 0x40;
        hb_set(&hb_scroll_down_test, 0, 3);
        hb_scroll_down(&hb_scroll_down_test, 1, 0x40);
    }

    // hb_scroll_left test, row 150: pre-clear column-bytes 0-2 (x=0-17) to
    // paper, set pixel x=6 (col-byte1 -> 0x60), scroll left by exactly 6px
    // (one whole column-byte). An exact 6px shift is equivalent to moving
    // whole bytes over by one column: col-byte0 should end up 0x60 (was
    // col-byte1), col-byte1 should end up 0x40 (was col-byte2, precleared
    // to paper). hb_scroll_left/right process every row of the canvas
    // passed in (unlike the rect/fill primitives, which are already scoped
    // to their own w/h) -- a 1-row sub-canvas keeps this test's cycle cost
    // to a single row's worth of work instead of scanning the full 200-row
    // screen, avoiding a Phosphoric cycle-budget timeout.
    hb_rect_fill(&hb, (const HiresClip *)0, 0, 150, 18, 1, false);
    hb_set(&hb, 6, 150);
    {
        HiresBitmap hb_row150;
        hb_init(&hb_row150, (uint8_t *)(HIRESVRAM + (uint16_t)150 * HIRES_ROW_BYTES), 1);
        hb_scroll_left(&hb_row150, 6, false);
    }

    // hb_scroll_right test, row 160: pre-clear column-bytes 0-1 (x=0-11) to
    // paper, set pixel x=5 (col-byte0, rightmost bit -> 0x41), scroll right
    // by 6px. The pixel at x=5 should move to x=11 (col-byte1's rightmost
    // bit -> 0x41); the vacated col-byte0 becomes the fill value (false ->
    // 0x40, all-paper).
    hb_rect_fill(&hb, (const HiresClip *)0, 0, 160, 12, 1, false);
    hb_set(&hb, 5, 160);
    {
        HiresBitmap hb_row160;
        hb_init(&hb_row160, (uint8_t *)(HIRESVRAM + (uint16_t)160 * HIRES_ROW_BYTES), 1);
        hb_scroll_right(&hb_row160, 6, false);
    }

    // Sprite test: a 6x2 all-ink sprite drawn over a paper background at
    // (0,170), then erased. Proves both halves of the save-under
    // technique: hspr_draw() changes the screen (paper -> ink), and
    // hspr_erase() restores the ORIGINAL background exactly (back to
    // paper) via the backed-up content, not just re-drawing a blank.
    {
        static uint8_t spr_image_data[2 * HIRES_ROW_BYTES];
        static uint8_t spr_backup_data[2 * HIRES_ROW_BYTES];
        HiresSprite spr;

        hspr_init(&spr, spr_image_data, spr_backup_data, 6, 2);
        hb_fill(&spr.image, 0x7F);   // sprite pixel data: solid ink

        hb_rect_fill(&hb, (const HiresClip *)0, 0, 170, 6, 2, false);   // background: solid paper
        hspr_draw(&hb, &spr, 0, 170);
        // After draw: row170/171 col0 should read 0x7f (the sprite).
        // Snapshotted to scratch offsets $1002/$1003 (still within the
        // 8000-byte HIRES buffer, unused elsewhere) since hspr_erase()
        // below overwrites row170/171 again before the final RAM dump.
        *(volatile uint8_t *)(HIRESVRAM + 0x1002) = *(volatile uint8_t *)(HIRESVRAM + (uint16_t)170 * HIRES_ROW_BYTES);
        *(volatile uint8_t *)(HIRESVRAM + 0x1003) = *(volatile uint8_t *)(HIRESVRAM + (uint16_t)171 * HIRES_ROW_BYTES);

        hspr_erase(&hb, &spr);
        // After erase: row170/171 col0 should be back to 0x40 (the
        // original background), left in place at its own address for the
        // final RAM dump (no need to snapshot -- nothing overwrites it
        // again before the program halts).
    }

    // hires_row_colors_range test: rows 175-179 stride 2 -> attributes
    // applied to 175/177/179 only, skipping 176/178. Sentinel value 0xAA
    // poked into the skipped rows first, to prove they're genuinely
    // untouched (not just coincidentally still ink/paper-looking).
    *(volatile uint8_t *)(HIRESVRAM + (uint16_t)176 * HIRES_ROW_BYTES) = 0xAA;
    *(volatile uint8_t *)(HIRESVRAM + (uint16_t)178 * HIRES_ROW_BYTES) = 0xAA;
    hires_row_colors_range(175, 179, 2, A_FWRED, A_BGGREEN);

    // hires_dissolve_* test: seed=12345's first accepted (in-range) LFSR
    // output is pixel index 44060 (hand-computed in Python when this test
    // was written) -> y=183, x=140 (column-byte 23, mask 0x08). Preclear
    // that byte, then dissolve_step(true) should set it to 0x48.
    *(volatile uint8_t *)(HIRESVRAM + (uint16_t)183 * HIRES_ROW_BYTES + 23) = 0x40;
    hires_dissolve_init(12345);
    hires_dissolve_step(&hb, hires_dissolve_next(), true);

    // rasterirq test: precleared marker at $1004 (=$B004) should stay 0x00
    // while interrupts are masked (hrirq_init()/hrirq_add() alone must be
    // inert -- proving programs that never call hrirq_start() are
    // unaffected), then become 0x99 once hrirq_start() enables interrupts
    // and Timer 1's already-free-running 100Hz IRQ (see oric.h's
    // TIMER1_100HZ) fires the handler at least once during the busy-wait
    // below (10000 cycles/tick at 1MHz; the wait is far longer than that).
    // hrirq_stop() re-disables interrupts afterward, restoring this
    // project's default IRQ-free state for the rest of the program (and
    // for the final RTI-less for(;;) halt below).
    //
    // The busy-wait uses a genuine volatile MEMORY WRITE each iteration
    // (not just a volatile loop counter) -- Oscar64's optimizer eliminated
    // an earlier version using only `volatile uint16_t wait` as the loop
    // variable entirely (confirmed in build/hires_test.asm: hrirq_start()
    // was immediately followed by hrirq_stop() with zero instructions
    // between them), matching this project's existing documented
    // "hardware/volatile accesses prevent loop collapse" convention
    // (see docs/keyboard.md's keyb_scan()-as-delay note) -- a bare
    // volatile *variable* wasn't enough, an actual volatile *store* was.
    *(volatile uint8_t *)(HIRESVRAM + 0x1004) = 0x00;
    hrirq_init();
    hrirq_add(100, hrirq_test_callback);
    hrirq_start();
    {
        volatile uint8_t *scratch = (volatile uint8_t *)(HIRESVRAM + 0x1005);
        uint16_t i;
        for (i = 0; i < 20000; i++)
            *scratch = (uint8_t)i;
    }
    hrirq_stop();

    for (;;)
        ;

    return 0;
}
