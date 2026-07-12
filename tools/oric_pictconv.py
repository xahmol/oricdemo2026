#!/usr/bin/env python3
"""oric_pictconv.py - convert a JPG/PNG image into Oric Atmos HIRES data.

In the spirit of the OSDK's C++ pictconv tool (github.com/Oric-Software-
Development-Kit/osdk, osdk/main/pictconv) but written fresh in Python: same
byte format and the same per-scanline colour-attribute optimisation idea
(see _recurse_line() below, a Python port of that tool's BlocOf6/RecurseLine
algorithm, not its C++ structure), plus the "Alternate Inverted Colors"
(AIC) demo-scene technique. Only the modes relevant to a HIRES bitmap are
implemented: mono, colored, aic -- not pictconv's RGB/RB/twilight-mask/
charmap/sam-hocevar/Atari/Limitless formats.

HIRES byte format (see include/oric.h for the authoritative C-side version):
  bit7 = invert, bit6 MUST be 1 for pixel data (else the ULA reads the byte
  as a colour attribute), bits5-0 = 6 pixels, bit5 = leftmost.
  Attribute byte (bit6=0): INK 0-7, PAPER 16-23.

Requires Pillow (see tools/requirements.txt): pip install -r tools/requirements.txt
"""

import argparse
import sys

from PIL import Image

# samhocevar mode (below) is a much more direct port than mono/colored/aic --
# see that section's own header comment for why, and for what was
# deliberately adapted rather than copied bit-for-bit.

# -------------------------------------------------------------------------
# Oric HIRES geometry (mirrors include/oric.h)
# -------------------------------------------------------------------------

HIRES_ROW_BYTES = 40
HIRES_ROWS = 200
HIRES_WIDTH_PX = HIRES_ROW_BYTES * 6   # 240
HIRES_SIZE = HIRES_ROW_BYTES * HIRES_ROWS  # 8000

# -------------------------------------------------------------------------
# Oric's 8-colour palette -- a pure 3-bit RGB cube (no C64-style shading;
# the AY-derived colour signal is digital, not analogue), index == the
# A_FW*/A_BG* colour code (0-7) in include/oric.h.
# -------------------------------------------------------------------------

PALETTE = [
    ("black",   (0,   0,   0)),
    ("red",     (255, 0,   0)),
    ("green",   (0,   255, 0)),
    ("yellow",  (255, 255, 0)),
    ("blue",    (0,   0,   255)),
    ("magenta", (255, 0,   255)),
    ("cyan",    (0,   255, 255)),
    ("white",   (255, 255, 255)),
]

PALETTE_NAMES = {name: idx for idx, (name, _rgb) in enumerate(PALETTE)}


def color_index(spec):
    """Resolve a CLI colour argument: either a palette name or 0-7 index."""
    if spec.lower() in PALETTE_NAMES:
        return PALETTE_NAMES[spec.lower()]
    idx = int(spec)
    if not 0 <= idx <= 7:
        raise argparse.ArgumentTypeError(f"colour index must be 0-7, got {idx}")
    return idx


def nearest_color_index(rgb, palette_indices):
    """Nearest-colour match (Euclidean, RGB) against a subset of PALETTE."""
    best_idx, best_dist = None, None
    for idx in palette_indices:
        _name, (r, g, b) = PALETTE[idx]
        dr, dg, db = rgb[0] - r, rgb[1] - g, rgb[2] - b
        dist = dr * dr + dg * dg + db * db
        if best_dist is None or dist < best_dist:
            best_idx, best_dist = idx, dist
    return best_idx


# -------------------------------------------------------------------------
# Image loading -- fit into 240x200 preserving aspect ratio, letterboxed
# (centered, padded with black) rather than stretched, so source images
# with a different aspect ratio don't come out visually distorted.
# -------------------------------------------------------------------------

def load_and_fit(path, width, height):
    img = Image.open(path).convert("RGB")
    src_w, src_h = img.size
    scale = min(width / src_w, height / src_h)
    new_w = max(1, round(src_w * scale))
    new_h = max(1, round(src_h * scale))
    img = img.resize((new_w, new_h), Image.LANCZOS)

    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    ox = (width - new_w) // 2
    oy = (height - new_h) // 2
    canvas.paste(img, (ox, oy))
    return canvas


# -------------------------------------------------------------------------
# Dithering -- quantize an RGB image against a fixed set of palette colours
# (given as a 2D list of pixel-index results, one per source pixel).
# -------------------------------------------------------------------------

# 4x4 Bayer ordered-dither threshold matrix (0-15), scaled to 0-255 at use.
_BAYER_4X4 = [
    [0,  8,  2,  10],
    [12, 4,  14, 6],
    [3,  11, 1,  9],
    [15, 7,  13, 5],
]


def dither_none(img, palette_indices):
    w, h = img.size
    px = img.load()
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            out[y][x] = nearest_color_index(px[x, y], palette_indices)
    return out


def dither_ordered(img, palette_indices):
    w, h = img.size
    px = img.load()
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            threshold = (_BAYER_4X4[y % 4][x % 4] + 0.5) / 16.0 * 255.0 - 127.5
            adj = (min(255, max(0, r + threshold)),
                   min(255, max(0, g + threshold)),
                   min(255, max(0, b + threshold)))
            out[y][x] = nearest_color_index(adj, palette_indices)
    return out


def dither_floyd_steinberg(img, palette_indices):
    w, h = img.size
    # Work in a float error buffer so accumulated diffusion isn't clipped
    # per-pixel before it's fully distributed.
    buf = [[list(img.getpixel((x, y))) for x in range(w)] for y in range(h)]
    out = [[0] * w for _ in range(h)]

    def clamp(v):
        return 0.0 if v < 0 else (255.0 if v > 255 else v)

    for y in range(h):
        for x in range(w):
            old = buf[y][x]
            idx = nearest_color_index(old, palette_indices)
            out[y][x] = idx
            _name, new = PALETTE[idx]
            err = [old[c] - new[c] for c in range(3)]

            def spread(dx, dy, weight):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    for c in range(3):
                        buf[ny][nx][c] = clamp(buf[ny][nx][c] + err[c] * weight)

            spread(1, 0, 7 / 16)
            spread(-1, 1, 3 / 16)
            spread(0, 1, 5 / 16)
            spread(1, 1, 1 / 16)

    return out


DITHERERS = {
    "none": dither_none,
    "ordered": dither_ordered,
    "floyd-steinberg": dither_floyd_steinberg,
}


# -------------------------------------------------------------------------
# Mono mode: a single global ink/paper pair for the whole image, no
# per-line attribute changes. Simplest, cheapest mode -- also the one used
# to prove the output-format/LOCI-loading plumbing before the harder
# colored-mode optimizer.
# -------------------------------------------------------------------------

def convert_mono(img, ink, paper, dither):
    ditherer = DITHERERS[dither]
    pixel_idx = ditherer(img, [ink, paper])

    out = bytearray(HIRES_SIZE)
    for y in range(HIRES_ROWS):
        row_off = y * HIRES_ROW_BYTES
        for cx in range(HIRES_ROW_BYTES):
            byte = 0x40
            base_x = cx * 6
            for bit in range(6):
                if pixel_idx[y][base_x + bit] == ink:
                    byte |= 0x20 >> bit
            out[row_off + cx] = byte
    return bytes(out)


# -------------------------------------------------------------------------
# Colored mode: per-scanline attribute-placement optimizer.
#
# Adapted from (not a literal port of) the OSDK pictconv tool's BlocOf6 /
# RecurseLine algorithm (oric_converter.h / oric_converter_colored.cpp,
# github.com/Oric-Software-Development-Kit/osdk, osdk/main/pictconv) --
# same core idea (walk each scanline's 40 six-pixel blocks left to right,
# backtracking over whether to keep the current ink/paper state, use the
# invert-bit trick, or spend a block on an attribute-change byte), with two
# deliberate differences from the original:
#
#   1. Attribute changes are tried for BOTH ink and paper (iterating all 8
#      candidate colours for each), where the original only ever changes
#      PAPER via attribute and treats ink changes as available solely by
#      chance ("ink already happens to match"). This is a strict
#      generalisation -- it can only find equal-or-better solutions.
#   2. The original's "change paper using inverse video" branch (writing
#      the attribute byte itself with bit7 set) is dropped: it reaches the
#      same *future* colour state as directly writing the plain attribute
#      for the complementary colour (already covered by trying all 8
#      candidates), and its exact effect on the attribute byte's own
#      displayed cell isn't confirmed by this project's hardware research
#      -- not worth the uncertainty for a purely cosmetic edge case.
#
# A block using >2 distinct colours is unrepresentable exactly (a HIRES
# byte only has 2 "slots": bit-clear and bit-set); such a block's 3rd+
# colour is silently folded into whichever of the first two colours it
# isn't (matching the original's behaviour), degrading gracefully rather
# than erroring -- proper dithering keeps this rare.
# -------------------------------------------------------------------------

DEFAULT_INK = 7      # ULA per-scanline default: INK=WHITE
DEFAULT_PAPER = 0    # ULA per-scanline default: PAPER=BLACK

# Backtracking-attempt budget per scanline, guarding against pathological
# (poorly dithered) inputs blowing up combinatorially; falls back to the
# same per-block "best effort" monochrome encoding pictconv itself falls
# back to when RecurseLine fails outright.
_SOLVE_BUDGET = 4000


def _make_block(colors6):
    """Mirror BlocOf6::AddColor: track up to the first 2 distinct colours
    seen across 6 source pixels (left to right), building a 6-bit value
    where bit5 (leftmost pixel) down to bit0 records which of the two
    colours each pixel used (0 = first colour seen, 1 = second). A 3rd+
    distinct colour is left as bit 0 (folded into the first colour) --
    same graceful degradation as the original tool."""
    colors = []
    value = 0
    for c in colors6:
        value <<= 1
        if not colors:
            colors.append(c)
        elif c == colors[0]:
            pass
        elif len(colors) == 1:
            colors.append(c)
            value |= 1
        elif c == colors[1]:
            value |= 1
        # else: 3rd+ colour, bit stays 0 (folded into colors[0])
    return colors, value & 0x3F


def _solve_line(blocks, idx, ink, paper, plan, budget):
    """Backtracking search over blocks[idx:], mirroring RecurseLine. plan[i]
    is filled with ('pixel', byte) or ('attr_ink'|'attr_paper', colour) on
    success. Returns False (and leaves plan untouched from idx on) if no
    solution exists from this state, or if the attempt budget runs out."""
    budget[0] -= 1
    if budget[0] <= 0:
        return False
    if idx == len(blocks):
        return True

    colors, value = blocks[idx]

    if len(colors) == 1:
        c0 = colors[0]

        if c0 == paper and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
            plan[idx] = ("pixel", 0x40)
            return True
        if c0 == ink and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
            plan[idx] = ("pixel", 0x7F)
            return True
        if c0 == (7 - paper) and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
            plan[idx] = ("pixel", 0xC0)
            return True
        if c0 == (7 - ink) and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
            plan[idx] = ("pixel", 0xFF)
            return True

        # Sacrifice this block as an attribute byte (its own source pixels
        # are lost -- the block becomes a coloured blank box on screen),
        # trying every candidate colour for whichever channel unsticks the
        # rest of the line.
        for new_paper in range(8):
            if new_paper == paper:
                continue
            if _solve_line(blocks, idx + 1, ink, new_paper, plan, budget):
                plan[idx] = ("attr_paper", new_paper)
                return True
        for new_ink in range(8):
            if new_ink == ink:
                continue
            if _solve_line(blocks, idx + 1, new_ink, paper, plan, budget):
                plan[idx] = ("attr_ink", new_ink)
                return True
        return False

    # 2-colour block: must match the current (ink, paper) pair exactly
    # (either assignment of colors[0]/colors[1] to paper/ink), directly or
    # globally inverted -- it can't be sacrificed as an attribute (that
    # would lose BOTH colours, not a valid transform).
    c0, c1 = colors[0], colors[1]

    if c0 == paper and c1 == ink and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
        plan[idx] = ("pixel", 0x40 | value)
        return True
    if c0 == ink and c1 == paper and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
        plan[idx] = ("pixel", 0x40 | (value ^ 0x3F))
        return True
    if c0 == (7 - paper) and c1 == (7 - ink) and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
        plan[idx] = ("pixel", 0xC0 | value)
        return True
    if c0 == (7 - ink) and c1 == (7 - paper) and _solve_line(blocks, idx + 1, ink, paper, plan, budget):
        plan[idx] = ("pixel", 0xC0 | (value ^ 0x3F))
        return True

    return False


def _fallback_line(blocks):
    """Per-pictconv: when RecurseLine can't (or the budget runs out),
    render each block's raw 2-colour bit pattern with no attribute changes
    at all -- colour-incorrect in general, but always renders *something*
    rather than erroring."""
    return [0x40 | value for _colors, value in blocks]


def convert_colored(img, dither):
    pixel_idx = DITHERERS[dither](img, list(range(8)))

    out = bytearray(HIRES_SIZE)
    for y in range(HIRES_ROWS):
        blocks = [
            _make_block(pixel_idx[y][cx * 6:cx * 6 + 6])
            for cx in range(HIRES_ROW_BYTES)
        ]

        plan = [None] * HIRES_ROW_BYTES
        budget = [_SOLVE_BUDGET]
        solved = _solve_line(blocks, 0, DEFAULT_INK, DEFAULT_PAPER, plan, budget)

        row_off = y * HIRES_ROW_BYTES
        if solved:
            for cx, step in enumerate(plan):
                kind, val = step
                if kind == "pixel":
                    out[row_off + cx] = val
                elif kind == "attr_paper":
                    out[row_off + cx] = 16 + val
                else:  # attr_ink
                    out[row_off + cx] = val
        else:
            for cx, byte in enumerate(_fallback_line(blocks)):
                out[row_off + cx] = byte

    return bytes(out)


# -------------------------------------------------------------------------
# AIC (Alternate Inverted Colors) mode: a different ink/paper pair on even
# vs. odd scanlines (see include/hires.h's HiresAIC for the C-side runtime
# equivalent). Unlike colored mode, the pair is FIXED per row (a "flat"
# per-parity palette, matching pictconv's own m_PaletteAIC[2][2] rather
# than running the block optimizer within each parity group -- a
# per-parity optimizer is a possible future improvement, not needed for a
# working v1).
#
# Columns 0-1 of every row hold that row's INK/PAPER attribute pair, baked
# directly into the output data (this is a self-contained data blob, not
# relying on a separate hires_aic_apply_range() runtime call) -- costing
# the first 12 pixels of every scanline, the same documented Oric hardware
# constraint colored mode's attribute insertions run into.
# -------------------------------------------------------------------------

def convert_aic(img, ink0, paper0, ink1, paper1, dither):
    ditherer = DITHERERS[dither]
    # Two independent full-image dithers, one per parity's 2-colour pair --
    # simpler than dithering only alternating rows, at the cost of computing
    # (and discarding) every other row's result from each pass.
    pixel_idx_even = ditherer(img, [ink0, paper0])
    pixel_idx_odd = ditherer(img, [ink1, paper1])

    out = bytearray(HIRES_SIZE)
    for y in range(HIRES_ROWS):
        even = (y % 2 == 0)
        ink, paper = (ink0, paper0) if even else (ink1, paper1)
        pixel_idx = pixel_idx_even if even else pixel_idx_odd
        row_off = y * HIRES_ROW_BYTES

        out[row_off + 0] = ink
        out[row_off + 1] = 16 + paper

        for cx in range(2, HIRES_ROW_BYTES):
            byte = 0x40
            base_x = cx * 6
            for bit in range(6):
                if pixel_idx[y][base_x + bit] == ink:
                    byte |= 0x20 >> bit
            out[row_off + cx] = byte

    return bytes(out)


# -------------------------------------------------------------------------
# samhocevar mode: OSDK pictconv's "-f6" ("Sam method (Img2Oric)") --
# per-block-of-6-pixels recursive lookahead search with a gamma-corrected
# perceptual error metric and full Floyd-Steinberg-like error diffusion
# (including into the row below, unlike mono/colored/aic's plain
# palette-quantize dithering above). Upstream doc: "generally gives much
# better results...albeit much much slower than other methods" -- true
# here too, this is a genuinely different order of magnitude slower than
# `colored` mode, expect real per-image runtimes in minutes.
#
# UNLIKE `colored`/`aic` above (which only borrow the general idea from
# pictconv's C++), this IS a close, near-line-by-line port of pictconv's
# own `oric_converter_samhocevar.cpp` (Sam Hocevar, "img2oric", 2008,
# WTFPL) -- the algorithm's quality comes from its exact, empirically-tuned
# constants (FS0-FS3 diffusion weights, the depth-based recursive
# lookahead, the specific perceptual-error formula), so approximating it
# "in the spirit of" the original (as `colored`/`aic` do) would lose
# exactly the thing that makes it worth having. Two deliberate deviations
# from the upstream source, both safety fixes rather than behaviour
# changes:
#
#   1. Upstream's `maxerror` parameter is accepted by `bestmove()` but
#      never actually read anywhere in its body (checked against the
#      source) -- dropped here as genuinely dead.
#   2. Upstream's flat pixel buffer only pads by exactly 1 extra column/
#      row beyond the real image, then reads up to ~30px past a block's
#      own right edge during the depth-2 recursive lookahead (and 1px
#      before a row's own left edge, for the down-left diffusion target)
#      -- both go out of the buffer's real bounds for edge blocks, which
#      C's pointer arithmetic tolerates silently (reads/writes bleed into
#      adjacent scanlines) but Python's bounds-checked lists do not. Uses
#      a generous zero-filled margin on all four sides instead (sized to
#      the actual worst-case lookahead distance for the configured
#      `depth`), so those bleed-reads become harmless reads of padding
#      instead of either crashing or (as upstream effectively does)
#      wrapping into unrelated scanline data.
# -------------------------------------------------------------------------

_SAM_DEPTH_DEFAULT = 2  # upstream's own DEPTH; 3 is documented as better/slower still.
_SAM_FS0, _SAM_FS1, _SAM_FS2, _SAM_FS3, _SAM_FSX = 15, 6, 9, 1, 32
_SAM_PAD = 2048
_SAM_CLAMP = 0x1000

# palette[c] in 16-bit-per-channel linear-ish units (0 or 0xffff), matching
# oric_converter_samhocevar.cpp's own `palette[8][6*3]` table (each entry
# repeated 6x there to paste whole blocks; not needed here since we index
# per-channel directly).
_SAM_PALETTE = [(r * 257, g * 257, b * 257) for _name, (r, g, b) in PALETTE]

# Command lookup table -- a DELIBERATE reduction from upstream
# oric_converter_samhocevar.cpp's own 34-entry `lookup[]`. Upstream's
# fg/bg-change commands come in "direct" (0x00-0x17) and "inverse video"
# (0x80-0x97) pairs: the "inverse" half assumes writing an attribute byte
# with bit7 set makes THAT byte's own (blank) screen cell display as the
# complementary colour instead of plain paper. This project's own HIRES
# convention (include/oric.h) only ever documents bit7 ("invert") as
# meaningful for PIXEL bytes (bit6=1) -- attribute bytes (bit6=0) are
# never written with bit7 set anywhere in include/hires.c, and `colored`
# mode's own header comment already flags this exact "inverse attribute
# byte" mechanism as unconfirmed by this project's hardware research, for
# the same reason. Including it here made bestmove() degenerate into only
# ever picking attribute-change commands it (incorrectly, per our model)
# believed already displayed the right colour, never falling back to
# real pixel-printing -- confirmed via a failing test (an all-white test
# block converted to a row of alternating attribute bytes, zero actual
# pixels set). Dropped the 16 "inverse attribute" candidates entirely,
# keeping direct fg-change (0x00-0x07), direct bg-change (0x10-0x17), and
# both pixel-printing commands (0x40 plain / 0xc0 inverted -- 0xc0's
# bit7-on-pixel-data meaning IS confirmed, see hires_invert_byte()).
_SAM_LOOKUP = (
    0x00, 0x04, 0x01, 0x05, 0x02, 0x06, 0x03, 0x07,
    0x10, 0x14, 0x11, 0x15, 0x12, 0x16, 0x13, 0x17,
    0x40, 0xc0,
)


def _sam_build_gamma_tables():
    size = _SAM_PAD * 2 + 256
    itoc = [0] * size
    ctoi = [0] * size
    for i in range(size):
        f = (i - _SAM_PAD) / 255.999
        if f >= 0:
            itoc[i] = int(65535.999 * (f ** (1 / 2.2)))
            ctoi[i] = int(65535.999 * (f ** 2.2))
        else:
            itoc[i] = -int(65535.999 * ((-f) ** (1 / 2.2)))
            ctoi[i] = -int(65535.999 * ((-f) ** 2.2))
    return itoc, ctoi


_SAM_ITOC_TABLE, _SAM_CTOI_TABLE = _sam_build_gamma_tables()
_SAM_TABLE_SIZE = len(_SAM_ITOC_TABLE)


def _sam_tdiv(a, b):
    """C-style truncating (toward zero) integer division, b > 0."""
    return a // b if a >= 0 else -((-a) // b)


def _sam_err_sq(d):
    """Mirrors `(a-b)/256 * (a-b)/256` -- left-to-right C operator
    precedence makes this `((d/256)*d)/256`, NOT `(d/256)**2`."""
    return _sam_tdiv(_sam_tdiv(d, 256) * d, 256)


def _sam_table_index(p):
    idx = _sam_tdiv(p, 256) + _SAM_PAD
    if idx < 0:
        return 0
    if idx >= _SAM_TABLE_SIZE:
        return _SAM_TABLE_SIZE - 1
    return idx


def _sam_itoc(p):
    return _SAM_ITOC_TABLE[_sam_table_index(p)]


def _sam_ctoi(p):
    return _SAM_CTOI_TABLE[_sam_table_index(p)]


def _sam_clamp(p):
    if p < -_SAM_CLAMP:
        return -_SAM_CLAMP
    if p > 0xffff + _SAM_CLAMP:
        return 0xffff + _SAM_CLAMP
    return p


def _sam_domove(command, bg, fg):
    if (command & 0x78) == 0x00:
        fg = command & 7
    elif (command & 0x78) == 0x10:
        bg = command & 7
    return bg, fg


def _sam_geterror(in_, inerr, out):
    """Perceptual error of replacing 6 source pixels ("in_", 18 ints) with
    6 chosen output pixels ("out", 18 ints), given "inerr" (3 ints) carried
    into the first pixel. Returns (error, outerr) -- outerr is the 3-int
    error carried past the last (6th) pixel, for the next block's own
    lookahead call only (NOT real persistent diffusion -- see the real
    diffusion pass in convert_samhocevar() below)."""
    tmperr = [0] * 27
    tmperr[0], tmperr[1], tmperr[2] = inerr[0], inerr[1], inerr[2]
    ret = 0

    for i in range(6):
        base = i * 3
        for c in range(3):
            a = _sam_clamp(in_[base + c] + tmperr[c])
            b = out[base + c]
            d = a - b
            tmperr[c] = _sam_tdiv(d * _SAM_FS0, _SAM_FSX)
            tmperr[c + base + 3] += _sam_tdiv(d * _SAM_FS1, _SAM_FSX)
            tmperr[c + base + 6] += _sam_tdiv(d * _SAM_FS2, _SAM_FSX)
            tmperr[c + base + 9] += _sam_tdiv(d * _SAM_FS3, _SAM_FSX)
            ret += _sam_err_sq(d)

    for i in range(4):
        base = i * 3
        for c in range(3):
            a = _sam_itoc(_sam_tdiv(in_[base + c] + in_[base + 3 + c] + in_[base + 6 + c], 3))
            b = _sam_itoc(_sam_tdiv(out[base + c] + out[base + 3 + c] + out[base + 6 + c], 3))
            ret += _sam_err_sq(a - b)

    for i in range(3):
        ret += _sam_err_sq(tmperr[i])

    return ret, tmperr[0:3]


def _sam_bestmove(in_, bg, fg, errvec, depth):
    """Recursive lookahead search over the reduced candidate command set
    (see _SAM_LOOKUP -- upstream had 34, this project's own hardware model
    only confirms half of them, see that table's own comment). Returns
    (command, error, out_rgb) -- out_rgb is the 18 chosen output pixel
    values (6 pixels x 3 channels) for the CURRENT block only."""
    voidrgb = _SAM_PALETTE[bg]
    voide, voidvec = _sam_geterror(in_, errvec, voidrgb * 6)

    if depth > 0:
        _cmd, statice, _rgb = _sam_bestmove(in_[18:], bg, fg, (0, 0, 0), depth - 1)
    else:
        statice = 0

    besterror = 0x7ffffff
    bestcommand = 0x10
    bestrgb = list(voidrgb) * 6

    for command in _SAM_LOOKUP:
        newbg, newfg = _sam_domove(command, bg, fg)

        if (command & 0x40) == 0x00 and newbg == bg and newfg == fg:
            continue

        if (command & 0xf8) == 0x00:
            curerror, rgb, vec = voide, voidrgb * 6, voidvec
        elif (command & 0xf8) == 0x10:
            rgb = _SAM_PALETTE[newbg] * 6
            curerror, vec = _sam_geterror(in_, errvec, rgb)
        else:
            if (command & 0x80) == 0x00:
                bgcolor, fgcolor = _SAM_PALETTE[bg], _SAM_PALETTE[fg]
            else:
                bgcolor, fgcolor = _SAM_PALETTE[7 - bg], _SAM_PALETTE[7 - fg]

            tmpvec = list(errvec)
            tmprgb = [0] * 18
            for i in range(6):
                base = i * 3
                vec1 = [0, 0, 0]
                vec2 = [0, 0, 0]
                smalle1 = smalle2 = 0
                for c in range(3):
                    px = _sam_clamp(in_[base + c] + tmpvec[c])
                    delta1 = px - bgcolor[c]
                    vec1[c] = _sam_tdiv(delta1 * _SAM_FS0, _SAM_FSX)
                    # NOTE: single division here (`delta1/256 * delta1`), NOT
                    # the double-divided _sam_err_sq() pattern used in
                    # _sam_geterror() -- verbatim from the upstream source's
                    # own `smalle1 += delta1 / 256 * delta1;`.
                    smalle1 += _sam_tdiv(delta1, 256) * delta1
                    delta2 = px - fgcolor[c]
                    vec2[c] = _sam_tdiv(delta2 * _SAM_FS0, _SAM_FSX)
                    smalle2 += _sam_tdiv(delta2, 256) * delta2
                if smalle1 < smalle2:
                    tmpvec = vec1
                    tmprgb[base:base + 3] = bgcolor
                else:
                    tmpvec = vec2
                    tmprgb[base:base + 3] = fgcolor
                    command |= (1 << (5 - i))

            curerror = _sam_geterror(in_, errvec, tmprgb)[0]
            rgb, vec = tmprgb, tmpvec

        if curerror > besterror:
            continue

        curerror = _sam_tdiv(curerror * 3, 4)

        if depth == 0:
            suberror = 0
        elif (command & 0x68) == 0x00:
            _cmd, suberror, _rgb = _sam_bestmove(in_[18:], newbg, newfg, vec, depth - 1)
            if newbg != bg:
                suberror = _sam_tdiv(suberror * 10, 8)
            elif newfg != fg:
                suberror = _sam_tdiv(suberror * 9, 8)
        else:
            suberror = statice

        if curerror + suberror < besterror:
            besterror = curerror + suberror
            bestcommand = command
            bestrgb = list(rgb)

    return bestcommand, besterror, bestrgb


def convert_samhocevar(img, depth=_SAM_DEPTH_DEFAULT, progress=False):
    width, height = HIRES_WIDTH_PX, HIRES_ROWS
    px = img.load()

    # See this section's own header comment (deviation #2): generous
    # zero-filled margin on all sides, sized to the worst-case lookahead
    # distance the recursion can read past a block's own 6 pixels.
    pad_l = 1
    pad_r = 6 * (depth + 2) + 1
    row_px = width + pad_l + pad_r
    stride = row_px * 3
    rows_total = height + 1  # +1: safe write target below the last real row

    src = [0] * (stride * rows_total)
    lookahead_len = 3 * 6 * (depth + 3)

    for y in range(height):
        row_off = y * stride
        for x in range(width):
            r, g, b = px[x, y]
            o = row_off + (x + pad_l) * 3
            src[o + 0] = _sam_ctoi(r * 0x101)
            src[o + 1] = _sam_ctoi(g * 0x101)
            src[o + 2] = _sam_ctoi(b * 0x101)

    out = bytearray(HIRES_SIZE)

    for y in range(height):
        if progress:
            print(f"\rsamhocevar: row {y + 1}/{height}", end="", file=sys.stderr)
        bg, fg = DEFAULT_PAPER, DEFAULT_INK
        row_off = y * stride
        for cx in range(width // 6):
            x = cx * 6
            base = row_off + (x + pad_l) * 3
            in_ = src[base: base + lookahead_len]
            command, _err, outrgb = _sam_bestmove(in_, bg, fg, (0, 0, 0), depth)

            for c in range(3):
                for i in range(6):
                    o = base + i * 3 + c
                    error = src[o] - outrgb[i * 3 + c]
                    src[o + 3] = _sam_clamp(src[o + 3] + _sam_tdiv(error * _SAM_FS0, _SAM_FSX))
                    src[o + stride - 3] += _sam_tdiv(error * _SAM_FS1, _SAM_FSX)
                    src[o + stride] += _sam_tdiv(error * _SAM_FS2, _SAM_FSX)
                    src[o + stride + 3] += _sam_tdiv(error * _SAM_FS3, _SAM_FSX)
                for i in range(-1, 7):
                    o = base + i * 3 + c + stride
                    src[o] = _sam_clamp(src[o])

            bg, fg = _sam_domove(command, bg, fg)
            out[y * HIRES_ROW_BYTES + cx] = command

    if progress:
        print(file=sys.stderr)

    return bytes(out)


# -------------------------------------------------------------------------
# Output writers
# -------------------------------------------------------------------------

def write_bin(data, path):
    with open(path, "wb") as f:
        f.write(data)


def write_header(data, path, label):
    lines = [
        f"// Generated by tools/oric_pictconv.py -- {len(data)} bytes.",
        f"const unsigned char {label}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


# -------------------------------------------------------------------------
# CLI
# -------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="source image (JPG/PNG/...)")
    ap.add_argument("output", help="output file (.bin or .h, see --format)")
    ap.add_argument("--mode", choices=["mono", "colored", "aic", "samhocevar"], default="mono")
    ap.add_argument("--dither", choices=list(DITHERERS), default="floyd-steinberg")
    ap.add_argument("--format", choices=["bin", "header"], default="bin")
    ap.add_argument("--label", default="oric_image", help="C array name for --format header")
    ap.add_argument("--width", type=int, default=HIRES_WIDTH_PX)
    ap.add_argument("--height", type=int, default=HIRES_ROWS)
    ap.add_argument("--ink", type=color_index, default=7, help="mono mode: ink colour (name or 0-7), default white")
    ap.add_argument("--paper", type=color_index, default=0, help="mono mode: paper colour (name or 0-7), default black")
    ap.add_argument("--aic-ink0", type=color_index, default=7, help="aic mode: even-row ink, default white")
    ap.add_argument("--aic-paper0", type=color_index, default=0, help="aic mode: even-row paper, default black")
    ap.add_argument("--aic-ink1", type=color_index, default=6, help="aic mode: odd-row ink, default cyan")
    ap.add_argument("--aic-paper1", type=color_index, default=0, help="aic mode: odd-row paper, default black")
    ap.add_argument("--samhocevar-depth", type=int, default=_SAM_DEPTH_DEFAULT,
                     help="samhocevar mode: recursive lookahead depth (upstream default 2; "
                          "3 is documented as better but slower still). Expect real per-image "
                          "runtimes of minutes, not seconds -- this mode is a much slower, "
                          "much more thorough search than colored/aic.")
    args = ap.parse_args()

    if args.width != HIRES_WIDTH_PX or args.height != HIRES_ROWS:
        print(f"ERROR: --width/--height must be {HIRES_WIDTH_PX}x{HIRES_ROWS} "
              f"(HIRES resolution) in this version", file=sys.stderr)
        return 1

    img = load_and_fit(args.input, args.width, args.height)

    if args.mode == "mono":
        data = convert_mono(img, args.ink, args.paper, args.dither)
    elif args.mode == "colored":
        data = convert_colored(img, args.dither)
    elif args.mode == "aic":
        data = convert_aic(img, args.aic_ink0, args.aic_paper0, args.aic_ink1, args.aic_paper1, args.dither)
    elif args.mode == "samhocevar":
        data = convert_samhocevar(img, depth=args.samhocevar_depth, progress=True)
    else:
        print(f"ERROR: --mode {args.mode} not implemented yet", file=sys.stderr)
        return 1

    if args.format == "bin":
        write_bin(data, args.output)
    else:
        write_header(data, args.output, args.label)

    print(f"Wrote {len(data)} bytes to {args.output} (mode={args.mode}, dither={args.dither})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
