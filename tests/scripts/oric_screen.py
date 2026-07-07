#!/usr/bin/env python3
"""Decode the 40x28 text screen from a Phosphoric --dump-ram-at 64KB RAM dump.

Screen RAM is at $BB80 (1120 = 40*28 bytes). Bytes with (byte & 0x7F) < 0x20
are serial attributes (ink/paper/charset, inverse-video bit in bit 7) and are
not characters; for codes >= 0x20, (byte & 0x7F) maps 1:1 onto ASCII.
"""
import sys
import argparse

SCREEN_ADDR = 0xBB80
COLS, ROWS = 40, 28


def load_grid(path):
    with open(path, "rb") as f:
        data = f.read()
    screen = data[SCREEN_ADDR:SCREEN_ADDR + COLS * ROWS]
    grid = []
    for row in range(ROWS):
        chars = []
        for col in range(COLS):
            byte = screen[row * COLS + col]
            code = byte & 0x7F
            chars.append(chr(code) if code >= 0x20 else None)
        grid.append(chars)
    return grid


def row_text(chars, stripped=True):
    if stripped:
        return "".join(c for c in chars if c is not None)
    return "".join(c if c is not None else " " for c in chars)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="path to 64KB RAM dump from --dump-ram-at")
    ap.add_argument("--find", help="search for STRING in screen text (per-row, attribute bytes stripped)")
    ap.add_argument("--ignore-case", action="store_true")
    ap.add_argument("--row", type=int, help="print only this row (0-27)")
    ap.add_argument("--bytes", help="dump raw RAM bytes as hex, format ADDR:LEN (e.g. 0xB608:8)")
    args = ap.parse_args()

    if args.bytes is not None:
        addr_str, len_str = args.bytes.split(":")
        addr, length = int(addr_str, 0), int(len_str, 0)
        with open(args.dump, "rb") as f:
            data = f.read()
        chunk = data[addr:addr + length]
        print(" ".join(f"{b:02x}" for b in chunk))
        return 0

    grid = load_grid(args.dump)

    if args.find is not None:
        needle = args.find.lower() if args.ignore_case else args.find
        for r, chars in enumerate(grid):
            text = row_text(chars, stripped=True)
            haystack = text.lower() if args.ignore_case else text
            if needle in haystack:
                print(f"FOUND row {r}: {text}")
                return 0
        print(f"NOT FOUND: {args.find!r}")
        return 1

    rows = [args.row] if args.row is not None else range(ROWS)
    for r in rows:
        print(f"{r:2d} |{row_text(grid[r], stripped=False)}|")
    return 0


if __name__ == "__main__":
    sys.exit(main())
