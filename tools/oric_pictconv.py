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
    ap.add_argument("--mode", choices=["mono", "colored", "aic"], default="mono")
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
