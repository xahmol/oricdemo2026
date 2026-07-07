#!/usr/bin/env python3
"""
mktap.py - Wrap an Oscar64 raw binary in an Oric Atmos .tap header

Usage: mktap.py <input.bin> <output.tap> <name> <load_addr>
  name       Program name, max 16 ASCII chars (truncated if longer)
  load_addr  Load address as hex with optional 0x prefix (e.g. 0x0500)

Tape header format (confirmed from locifilemanager v1 tapehdr.s):
  Bytes 0-2:   $16 $16 $16          Sync bytes
  Byte  3:     $24                  Beginning-of-header marker
  Bytes 4-5:   $00 $00              Reserved flags ($2B0/$2AF)
  Byte  6:     $80                  Language flag: $80 = machine code
  Byte  7:     $C7                  Auto-run flag: $C7 = run, $00 = load only
  Bytes 8-9:   end_hi, end_lo       End address (last byte, inclusive), BIG-ENDIAN
  Bytes 10-11: start_hi, start_lo   Start/load address, BIG-ENDIAN
  Byte  12:    $00                  Reserved
  Bytes 13+:   filename\\0          Up to 16 ASCII chars + null terminator

Based on: locifilemanager v1 src/tapehdr.s by Debrune Jérome / Greg King / Xander Mol
"""

import sys
import struct


def main():
    if len(sys.argv) != 5:
        print(
            "Usage: mktap.py <input.bin> <output.tap> <name> <load_addr>",
            file=sys.stderr,
        )
        print("  Example: mktap.py build/locifm.bin build/locifm.tap LOCIFM 0x0500",
              file=sys.stderr)
        sys.exit(1)

    in_path  = sys.argv[1]
    out_path = sys.argv[2]
    name     = sys.argv[3][:16]          # max 16 chars
    load     = int(sys.argv[4], 0)       # handles 0x prefix

    with open(in_path, "rb") as f:
        data = f.read()

    if not data:
        print(f"ERROR: {in_path} is empty", file=sys.stderr)
        sys.exit(1)

    size     = len(data)
    end_addr = load + size - 1           # inclusive last byte

    # Sanity checks
    if load < 0x0500:
        print(f"WARNING: load address {load:#06x} is below $0500 — may overlap system RAM",
              file=sys.stderr)
    if end_addr > 0xBB7F:
        print(f"WARNING: binary end {end_addr:#06x} extends into or past screen RAM ($BB80)",
              file=sys.stderr)
    if end_addr > 0xFFFF:
        print(f"ERROR: binary too large (end address {end_addr:#06x} > $FFFF)",
              file=sys.stderr)
        sys.exit(1)

    # Build header
    header = bytearray()
    header += bytes([0x16, 0x16, 0x16])                       # sync bytes
    header += bytes([0x24])                                     # header marker
    header += bytes([0x00, 0x00])                               # reserved flags
    header += bytes([0x80])                                     # machine code
    header += bytes([0xC7])                                     # autorun
    header += bytes([(end_addr >> 8) & 0xFF, end_addr & 0xFF]) # end address, big-endian
    header += bytes([(load >> 8) & 0xFF, load & 0xFF])         # start address, big-endian
    header += bytes([0x00])                                     # reserved
    header += name.encode("ascii") + b"\x00"                   # filename + null

    with open(out_path, "wb") as f:
        f.write(bytes(header))
        f.write(data)

    print(
        f"Created {out_path}: "
        f"load={load:#06x} end={end_addr:#06x} "
        f"size={size} ({size // 1024}.{(size % 1024) * 10 // 1024} KB) "
        f"name={name!r}"
    )


if __name__ == "__main__":
    main()
