// section_background.c - static sky + creek background (HIRES mode)
//
// Procedurally drawn via plain PAPER colour bands: sky for the upper rows,
// creek (water) for a band at the bottom. Ink stays white everywhere
// (matching main.c's whole-screen baseline) -- only PAPER varies by band,
// so section_bird.c's HxsprColor restore_ink (assumes a known, fixed ink
// baseline) remains valid wherever the bird flies, regardless of which
// band it's currently over.
//
// KNOWN ISSUE -- no pixel content (pattern fill / drawn line) in this
// band, only plain colour: a confirmed, currently-unresolved Phosphoric
// rendering bug means any hb_rect_pattern/hb_rect_fill/hb_set content
// drawn in the upper-to-high rows (~row 170+, empirically also reproduced
// starting well below that), COMBINED with main.c's hires_footer_enable()
// being active, makes EVERY row's PAPER colour render identically (as if
// only the LAST hires_row_colors() call took effect) despite the RAM
// itself holding byte-correct, genuinely different attribute values per
// band (verified directly via RAM dump, not just a screenshot glance).
// Confirmed NOT a transient/timing artifact (persists from early capture
// through 30M+ cycles) and NOT specific to hb_rect_pattern specifically
// (hb_rect_fill reproduces it too) or to any particular row range (row
// 10-36 is fine; anywhere overlapping roughly the creek's intended band
// is not) -- root cause not yet found despite extensive bisection (see
// project memory: floppy_hires bug's sibling investigation for the
// bisection method). Plain colour bands (this file's current approach)
// are confirmed correct at the byte AND pixel level. The originally
// planned meandering water-line + shimmer-pattern texture is deferred
// until this is properly root-caused -- don't re-add pixel content here
// without re-testing against hires_footer_enable() being active.

#include "oric.h"
#include "hires.h"
#include "section_background.h"

#define SKY_PAPER    A_BGCYAN
#define CREEK_PAPER  A_BGBLUE
#define CREEK_TOP    172u

void section_background_run(const HiresBitmap *screen)
{
    uint8_t y;

    (void)screen;   // unused now -- direct hires_row_colors() calls, kept for API symmetry with other sections

    for (y = 0; y < CREEK_TOP; y++)
        hires_row_colors(y, A_FWWHITE, SKY_PAPER);
    for (y = CREEK_TOP; y < HIRES_ROWS; y++)
        hires_row_colors(y, A_FWWHITE, CREEK_PAPER);
}
