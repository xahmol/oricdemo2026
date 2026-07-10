// section_background.h - static sky/bank/trees + animated river (HIRES mode)

#ifndef SECTION_BACKGROUND_H
#define SECTION_BACKGROUND_H

#include "hires.h"

// Draws the sky, green bank, trees, and river once. Call before any section
// that draws on top of it (e.g. section_bird_run()).
void section_background_run(const HiresBitmap *screen);

// Advances the river's shimmer animation one step -- call once per
// main-loop tick, alongside other sections' own tick functions (see
// main.c). Paces itself internally (see RIVER_SCROLL_EVERY in
// section_background.c), same convention as section_clouds_tick().
void section_background_tick(const HiresBitmap *screen);

// Returns true and sets *out_ink if `col` is EXACTLY one of the trees'
// own static ink-bracket columns (see section_background.c's
// draw_tree_ink()) AND that tree's own row range genuinely overlaps
// [row_min, row_max] -- false otherwise (including when the column
// numerically matches a tree bracket but the two row ranges don't
// overlap at all, e.g. the bird flying high in the sky). A moving sprite
// whose own colour bracket (see sprite.h's HxsprColor) might land on one
// of these exact columns should use *out_ink for its WHOLE draw call
// instead of its own normal ink/restore_ink value, so the call writes the
// CORRECT byte in one atomic step -- never a wrong one that a later
// "fix-up" call has to correct after the fact. That fix-up approach was
// tried first and rejected: since hxspr_draw()'s own body loop takes long
// enough (a few thousand cycles) to be a substantial fraction of a tick's
// total time, a fix-up call running only after it fully returns left a
// real, frequently-visible window (not a rare one-frame flicker) where
// the sprite's own bracket value was wrong.
//
// Known residual, accepted rather than engineered away: hxspr_draw() uses
// ONE ink value for its entire height, but a tall sprite's row range can
// only PARTIALLY overlap a tree's own (narrower) row range -- when it
// does, this function's "any overlap -> use the tree's colour for the
// whole column" answer is technically wrong for the sprite's OWN rows
// that DON'T overlap the tree (they'll show the tree's colour instead of
// the sprite's normal default for one tick). This is purely cosmetic and
// self-correcting: hxspr_erase()'s own byte-exact backup/restore doesn't
// care what colour was used to draw, only what was really there before,
// so the very next erase puts everything back correctly regardless. A
// fully precise per-row fix would need hxspr_draw() to support a
// per-row ink override, a bigger change not made here.
bool section_background_tree_bracket_ink(uint8_t col, uint8_t row_min, uint8_t row_max, uint8_t *out_ink);

#pragma compile("section_background.c")

#endif // SECTION_BACKGROUND_H
