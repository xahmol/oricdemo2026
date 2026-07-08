#!/usr/bin/env python3
"""extract_bootsector.py - build tools/floppy/bootsector_microdisc.c's final
256-byte boot sector for the floppy-disk build target (see docs/floppy.md).

Two things this compiled program's own .c source cannot do (see that
file's own header comment for why):

1. Prepend the Microdisc EPROM's required 23-byte sanity-check header --
   a hardware-protocol-mandated blob, not meaningful code, that Oscar64
   cannot emit from a named __asm block (no raw byte-literal directive).
   Traced directly from OSDK's reference source (per osdk.org's stated
   reuse terms -- see bootsector_microdisc.c's own attribution comment):
   github.com/Oric-Software-Development-Kit/osdk,
   osdk/main/Osdk/_final_/sample/floppybuilder/code/sector_2-microdisc.asm
   -- the ".byt $00,$00,$FF,$00,$D0,$9F,$D0,$9F,$02,$B9,$01,$00,$FF,$00,
   $00,$B9,$E4,$B9,$00,$00,$E6,$12,$00" line, 23 bytes exactly.

2. Extract JUST the "bootsector" labeled block's compiled bytes, not the
   whole .bin: Oscar64's default runtime (bootsector_microdisc.c has no
   -rt= override) wraps it in CRT/startup scaffolding (a "startup" region
   plus a "main" region containing an `int main(void) { jsr bootsector }`
   trampoline) -- everything the disk's boot sector actually needs is the
   "bootsector" label's own bytes, found via the compiler's own .map file
   (bootsector_microdisc.c is compiled with -g to produce one), not the
   whole linked program.

Usage: extract_bootsector.py <compiled.bin> <compiled.map> <output.bin>

Output is exactly 256 bytes: the 23-byte header, the extracted
"bootsector" bytes, then zero-padding (WriteSector's own zero-pads to 256
too, but this script checks the fit explicitly so a boot sector that
outgrows 256 bytes total is a clear, immediate build error, not a silently
truncated disk).
"""

import re
import sys

EPROM_HEADER = bytes([
    0x00, 0x00, 0xFF, 0x00, 0xD0, 0x9F, 0xD0, 0x9F, 0x02, 0xB9,
    0x01, 0x00, 0xFF, 0x00, 0x00, 0xB9, 0xE4, 0xB9, 0x00, 0x00,
    0xE6, 0x12, 0x00,
])
assert len(EPROM_HEADER) == 23

SECTOR_SIZE = 256


def find_label_range(map_text: str, label: str):
    """Parses the "objects" section of an Oscar64 .map file for a line
    like "090a - 09c2 : bootsector, NATIVE_CODE:code" and returns
    (start_addr, end_addr) as integers. Raises if not found."""
    pattern = re.compile(
        r'^([0-9a-fA-F]{4,6})\s*-\s*([0-9a-fA-F]{4,6})\s*:\s*' + re.escape(label) + r'\b',
        re.MULTILINE,
    )
    m = pattern.search(map_text)
    if not m:
        raise ValueError(f"label '{label}' not found in map file")
    return int(m.group(1), 16), int(m.group(2), 16)


def find_bin_load_address(map_text: str) -> int:
    """The .bin file is a contiguous dump starting at the lowest
    CODE/DATA/BSS region's start address (the "regions" section's first
    non-zeropage entry) -- needed to convert a label's absolute address
    into a file offset."""
    in_regions = False
    for line in map_text.splitlines():
        if line.strip() == "regions":
            in_regions = True
            continue
        if not in_regions:
            continue
        if line.strip() == "" or line.strip() == "objects":
            break
        m = re.match(r'^([0-9a-fA-F]{4,6})\s*-\s*[0-9a-fA-F]{4,6}\s*:.*,\s*(\w+)\s*$', line.strip())
        if m and m.group(2).lower() != "zeropage":
            return int(m.group(1), 16)
    raise ValueError("no non-zeropage region found in map file")


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <compiled.bin> <compiled.map> <output.bin>", file=sys.stderr)
        sys.exit(1)

    bin_path, map_path, out_path = sys.argv[1:4]

    with open(bin_path, "rb") as f:
        compiled = f.read()
    with open(map_path, "r") as f:
        map_text = f.read()

    label_start, label_end = find_label_range(map_text, "bootsector")
    load_address = find_bin_load_address(map_text)

    offset = label_start - load_address
    length = label_end - label_start

    if offset < 0 or offset + length > len(compiled):
        raise ValueError(
            f"computed range [{offset}:{offset+length}] falls outside the "
            f".bin file (size {len(compiled)}) -- load_address/map mismatch?"
        )

    code_bytes = compiled[offset:offset + length]

    total = len(EPROM_HEADER) + len(code_bytes)
    if total > SECTOR_SIZE:
        raise ValueError(
            f"boot sector too large: {len(EPROM_HEADER)}-byte header + "
            f"{len(code_bytes)}-byte code = {total} bytes, exceeds "
            f"{SECTOR_SIZE}-byte sector limit by {total - SECTOR_SIZE}"
        )

    sector = EPROM_HEADER + code_bytes + bytes(SECTOR_SIZE - total)
    assert len(sector) == SECTOR_SIZE

    with open(out_path, "wb") as f:
        f.write(sector)

    print(f"extract_bootsector: {len(code_bytes)} code bytes (label {hex(label_start)}-{hex(label_end)}, "
          f"file offset {offset}) + {len(EPROM_HEADER)}-byte header -> {out_path} ({SECTOR_SIZE} bytes)")


if __name__ == "__main__":
    main()
