#!/usr/bin/env python3
"""oric_floppybuilder.py - build a bootable Oric Atmos floppy disk image.

In the spirit of the OSDK's C++ FloppyBuilder tool (github.com/
Oric-Software-Development-Kit/osdk, osdk/main/FloppyBuilder), written fresh
in Python: same script description-language syntax and the same .dsk byte
format (MFM_DISK header, side-major/track-major body, gap-table-driven
per-track layout, CRC-16/CCITT sector checksums, round-robin sector
interleave), traced directly from FloppyBuilder.cpp/Floppy.cpp -- not a
line-by-line port of that C++ structure. OSDK's own stated terms
(https://osdk.org/index.php?page=main) permit using/modifying/extending
"the SDK or any part of it" for anything, the only restrictions being not
reselling the SDK's own source code and not claiming ownership of it --
this tool follows that: same behaviour, freshly written.

Deliberate scope cuts versus the reference (see docs/floppy.md):
  - SetCompressionMode FilePack is a hard error -- only None is implemented.
    FilePack is a specific LZ77 variant, a genuinely separate algorithm not
    needed for one program + a handful of uncompressed assets.
  - LoadDiskTemplate is not implemented (fresh-disk builds only) -- this is
    what lets the per-sector byte-offset table be computed directly from
    the gap-table formula, instead of built by re-scanning freshly-
    formatted MFM bytes for A1/FE/FB markers (the reference's actual
    approach, needed there for foreign/templated disks whose layout isn't
    otherwise known).
  - The generated header is plain C only -- the reference's dual
    #ifdef ASSEMBLER/#ifdef LOADER xa65-compatibility trick is dropped,
    since this project's loader is Oscar64 C, not xa65 (nothing here ever
    needs the assembler-syntax half of that file).

Usage: oric_floppybuilder.py <init|build|extract> <script.txt> [-Dname=value ...]
"""

import argparse
import re
import struct
import sys
from pathlib import Path

# -------------------------------------------------------------------------
# CRC-16/CCITT, table-driven -- traced directly from Floppy.cpp's crctab[256]
# and compute_crc() (crc = 0xFFFF initial; crc = (crc<<8) ^ crctab[(crc>>8)^byte]).
# -------------------------------------------------------------------------

def _build_crc_table():
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
        table.append(crc)
    return table


_CRC_TABLE = _build_crc_table()


def compute_crc(data: bytes) -> bytes:
    """Returns the 2-byte (big-endian) CRC-16/CCITT for `data`, matching
    Floppy.cpp's compute_crc() exactly (init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc = ((crc << 8) ^ _CRC_TABLE[(crc >> 8) ^ byte]) & 0xFFFF
    return bytes([(crc >> 8) & 0xFF, crc & 0xFF])


# -------------------------------------------------------------------------
# Disk geometry constants
# -------------------------------------------------------------------------

SECTOR_SIZE = 256
TRACK_SIZE = 6400
HEADER_SIZE = 256

# 15/16/17 sectors/track -> gap1=72,gap2=34,gap3=50; 18 -> gap1=40,gap2=34,gap3=34
# (18-sector value per OSDK's own changelog: "changed to solve compatibility
# issues with Cumulus", not the older 12/34/46 some historical tools used).
GAP_TABLE = {
    15: (72, 34, 50),
    16: (72, 34, 50),
    17: (72, 34, 50),
    18: (40, 34, 34),
}


def compute_sector_data_offsets(num_sectors: int, gap1: int, gap2: int, gap3: int, interleave: int):
    """Returns (sector_order, data_offset_by_logical_sector), computed
    directly from the gap-table formula -- see the module docstring's scope
    cuts for why this isn't built by re-scanning formatted MFM bytes the
    way the reference tool does.

    sector_order[physical_slot] = logical sector number (1-indexed) placed
    in that physical slot, via the same round-robin interleave algorithm
    Floppy.cpp's CreateDisk() uses.

    data_offset_by_logical_sector[logical_sector-1] = byte offset (from the
    start of the track, i.e. matching Floppy.cpp's m_SectorOffset) of that
    logical sector's 256-byte data payload.
    """
    sector_order = [0] * num_sectors
    offset = 0
    for sector in range(num_sectors):
        while sector_order[offset] != 0:
            offset += 1
            if offset >= num_sectors:
                offset = 0
        sector_order[offset] = sector + 1
        offset += interleave
        if offset >= num_sectors:
            offset = 0

    # Per physical slot: 10-byte ID field (4 marker + track/side/sector/size
    # + 2 CRC) + gap2 + 4-byte data marker + 256 data bytes + 2 CRC + gap3.
    data_offset_by_logical = [0] * num_sectors
    slot_start = gap1
    for slot in range(num_sectors):
        data_offset = slot_start + 10 + gap2 + 4
        logical = sector_order[slot]
        data_offset_by_logical[logical - 1] = data_offset
        slot_start += 10 + gap2 + 4 + SECTOR_SIZE + 2 + gap3

    return sector_order, data_offset_by_logical


def format_track(num_sectors: int, gap1: int, gap2: int, gap3: int, sector_order) -> bytearray:
    """Builds one 6400-byte formatted (but data-zeroed) track, matching
    Floppy.cpp's CreateDisk() track-fill loop exactly."""
    track = bytearray()
    track.extend(b"\x4E" * (gap1 - 12))
    for slot in range(num_sectors):
        track.extend(b"\x00" * 12)
        track.extend(b"\xA1" * 3)
        track.append(0xFE)
        track.extend(b"\x00" * 6)  # track/side/sector/size + 2 CRC, filled in later
        track.extend(b"\x4E" * (gap2 - 12))
        track.extend(b"\x00" * 12)
        track.extend(b"\xA1" * 3)
        track.append(0xFB)
        track.extend(b"\x00" * (SECTOR_SIZE + 2))  # 256 data + 2 CRC, filled in later
        track.extend(b"\x4E" * (gap3 - 12))
    while len(track) < TRACK_SIZE:
        track.append(0x4E)
    assert len(track) == TRACK_SIZE
    return track


def write_track_id_fields(track: bytearray, track_num: int, side: int, num_sectors: int,
                           gap1: int, gap2: int, gap3: int, sector_order):
    """Fills in the track/side/sector/size bytes and both CRCs for every
    sector's ID field on a freshly-formatted track (the data field's CRC
    gets recomputed separately, each time real data is written there)."""
    slot_start = gap1
    for slot in range(num_sectors):
        id_offset = slot_start  # start of "A1 A1 A1 FE"
        track[id_offset + 4] = track_num
        track[id_offset + 5] = side
        track[id_offset + 6] = sector_order[slot]
        track[id_offset + 7] = 1
        crc = compute_crc(bytes(track[id_offset:id_offset + 8]))
        track[id_offset + 8:id_offset + 10] = crc
        slot_start += 10 + gap2 + 4 + SECTOR_SIZE + 2 + gap3


# -------------------------------------------------------------------------
# Script tokenizer -- traced from FloppyBuilder.cpp's GetNextToken(): "..."
# and {...} and [...] are single tokens (delimiters kept, stripped later);
# unquoted tokens split on whitespace; ';' truncates the rest of the line
# as a comment (stripped before tokenizing, matching the reference).
# -------------------------------------------------------------------------

def tokenize_line(line: str):
    line = line.split(";", 1)[0]
    tokens = []
    metadata = {}
    i = 0
    n = len(line)
    while i < n:
        while i < n and line[i] in " \t":
            i += 1
        if i >= n:
            break
        start = i
        if line[i] in '"{[':
            match = {'"': '"', '{': '}', '[': ']'}[line[i]]
            i += 1
            while i < n and line[i] != match:
                i += 1
            if i < n:
                i += 1  # include the closing delimiter
            raw = line[start:i]
        else:
            while i < n and line[i] not in " \t":
                i += 1
            raw = line[start:i]
        if raw.startswith("[") and raw.endswith("]"):
            inner = raw[1:-1]
            if ":" in inner:
                key, value = inner.split(":", 1)
                metadata[key] = value
        else:
            if raw.startswith('"') and raw.endswith('"') and len(raw) >= 2:
                raw = raw[1:-1]
            tokens.append(raw)
    return tokens, metadata


def convert_address(text: str) -> int:
    """Matches the reference's ConvertAdress(): accepts '$hex', '0xhex', or
    plain decimal."""
    text = text.strip()
    if text.startswith("$"):
        return int(text[1:], 16)
    return int(text, 0)


# -------------------------------------------------------------------------
# FileEntry / Builder
# -------------------------------------------------------------------------

class FloppyBuilderError(Exception):
    pass


class FileEntry:
    def __init__(self, path, start_track, start_sector, size, disk_offset):
        self.path = path
        self.start_track = start_track
        self.start_sector = start_sector
        self.size = size
        self.disk_offset = disk_offset

    def sector_field(self) -> int:
        # Compression-flag bit (bit7) always 0 in this build -- FilePack is
        # not implemented (see module docstring). Kept as a method (not a
        # plain attribute) so the flag's meaning stays documented at the
        # one place it would need to change if compression were ever added.
        return self.start_sector


class Disk:
    def __init__(self, sides: int, tracks: int, sectors: int, interleave: int = 1):
        if sides != 2:
            raise FloppyBuilderError("numberOfSides has to be 2 (so far)")
        if not (1 <= interleave < sectors):
            raise FloppyBuilderError(f"{interleave} is not a valid sector interleave value")
        if sectors not in GAP_TABLE:
            raise FloppyBuilderError(
                f"{sectors} is an unrealistic sectors per track number, "
                f"supported values are 15, 16, 17 and 18"
            )
        if not (30 <= tracks <= 82):
            raise FloppyBuilderError(f"{tracks} is not a valid track count (must be 30-82)")

        self.sides = sides
        self.tracks = tracks
        self.sectors = sectors
        self.interleave = interleave
        self.gap1, self.gap2, self.gap3 = GAP_TABLE[sectors]

        self.sector_order, self.sector_data_offset = compute_sector_data_offsets(
            sectors, self.gap1, self.gap2, self.gap3, interleave
        )

        header = bytearray(HEADER_SIZE)
        header[0:8] = b"MFM_DISK"
        header[8:12] = struct.pack("<I", sides)
        header[12:16] = struct.pack("<I", tracks)
        header[16:20] = struct.pack("<I", 1)
        self.buffer = bytearray(header)

        for side in range(sides):
            for track in range(tracks):
                t = format_track(sectors, self.gap1, self.gap2, self.gap3, self.sector_order)
                write_track_id_fields(t, track, side, sectors, self.gap1, self.gap2, self.gap3,
                                       self.sector_order)
                self.buffer.extend(t)

        assert len(self.buffer) == HEADER_SIZE + sides * tracks * TRACK_SIZE

    def track_offset(self, absolute_track: int) -> int:
        # Tracks are numbered continuously 0..2*tracks-1 across both sides
        # (side 1's tracks are self.tracks..2*self.tracks-1), matching the
        # reference's own m_CurrentTrack convention.
        return HEADER_SIZE + absolute_track * TRACK_SIZE

    def sector_disk_offset(self, absolute_track: int, sector: int) -> int:
        return self.track_offset(absolute_track) + self.sector_data_offset[sector - 1]

    def write_sector_data(self, absolute_track: int, sector: int, data: bytes):
        """Zero-pads/truncates `data` to 256 bytes, writes it, and
        recomputes the data field's CRC (over the 4 marker bytes
        immediately preceding it plus the 256 data bytes) -- matching
        WriteSector/WriteFile's `compute_crc(...-4, 4+256)` exactly."""
        offset = self.sector_disk_offset(absolute_track, sector)
        payload = (data + b"\x00" * SECTOR_SIZE)[:SECTOR_SIZE]
        self.buffer[offset:offset + SECTOR_SIZE] = payload
        crc = compute_crc(bytes(self.buffer[offset - 4:offset + SECTOR_SIZE]))
        self.buffer[offset + SECTOR_SIZE:offset + SECTOR_SIZE + 2] = crc

    def read_sector_data(self, absolute_track: int, sector: int) -> bytes:
        offset = self.sector_disk_offset(absolute_track, sector)
        return bytes(self.buffer[offset:offset + SECTOR_SIZE])


class Builder:
    def __init__(self, mode: str, defines: dict):
        self.mode = mode
        self.allow_missing = mode in ("init", "extract")
        self.extract = mode == "extract"
        self.defines = dict(defines)  # -D command-line defines, seeded first
        self.define_list = []  # ordered list of (name, value) for the header
        self.disk: Disk | None = None
        self.current_track = 0
        self.current_sector = 1
        self.loader_load_address = None
        self.file_entries: list[FileEntry] = []
        self.script_dir = Path(".")
        self.compression_mode = "None"
        self.extract_requests = []  # (out_path, track, sector, count), extract mode only

    # -- helpers ----------------------------------------------------------

    def _resolve(self, text: str) -> str:
        """Substitutes {DefineName} placeholders (the -D/AddDefine values,
        e.g. {DEMO_BIN}) in a path/argument token -- this project's own
        addition to drive the script from Makefile variables, matching the
        reference's own use of curly-brace substitution for {FileIndex}
        etc., just extended to arbitrary -D-supplied names."""
        def repl(m):
            name = m.group(1)
            if name in self.defines:
                return str(self.defines[name])
            return m.group(0)
        return re.sub(r"\{(\w+)\}", repl, text)

    def _load_file(self, path: str) -> bytes:
        resolved = self._resolve(path)
        p = self.script_dir / resolved
        if not p.is_file():
            if self.allow_missing:
                return b"Place holder file generated by FloppyBuilder"
            raise FloppyBuilderError(f"Can't open file '{path}'")
        return p.read_bytes()

    def _advance_sector(self) -> bool:
        self.current_sector += 1
        if self.current_sector > self.disk.sectors:
            self.current_sector = 1
            self.current_track += 1
            if self.current_track >= self.disk.tracks * self.disk.sides:
                return False
        return True

    def _add_define_raw(self, name: str, value: str):
        self.define_list.append((name, value))

    def _add_define_for_last_file(self, name: str, value: str):
        """Substitutes {FileIndex}/{FileSize}/{FileTrack}/{FileSector}/
        {FileDiskOffset} against the last AddFile'd entry, matching
        Floppy::AddDefine's exact substitution set (FileSizeCompressed is
        always equal to FileSize here -- compression isn't implemented)."""
        if "{File" in name or "{File" in value:
            if not self.file_entries:
                raise FloppyBuilderError(
                    f"AddDefine {name} {value}: a {{File* directive can only be used after a file was added"
                )
            entry = self.file_entries[-1]
            subs = {
                "{FileIndex}": str(len(self.file_entries) - 1),
                "{FileSize}": str(entry.size),
                "{FileTrack}": str(entry.start_track),
                "{FileSector}": str(entry.sector_field()),
                "{FileDiskOffset}": str(entry.disk_offset),
                "{FileSizeCompressed}": str(entry.size),
            }
            for k, v in subs.items():
                name = name.replace(k, v)
                value = value.replace(k, v)
        self._add_define_raw(name, value)

    # -- commands -----------------------------------------------------

    def cmd_define_disk(self, args):
        if len(args) not in (3, 4):
            raise FloppyBuilderError("syntax is 'DefineDisk sides tracks sectors [interleave]'")
        sides, tracks, sectors = (int(a) for a in args[:3])
        interleave = int(args[3]) if len(args) == 4 else 1
        self.disk = Disk(sides, tracks, sectors, interleave)

    def cmd_set_position(self, args):
        if len(args) != 2:
            raise FloppyBuilderError("syntax is 'SetPosition track sector'")
        track, sector = int(args[0]), int(args[1])
        # Bounds replicate the reference's ACTUAL check (0-41 both), not its
        # comment's claim (1-17 for sector) -- see module docstring.
        if not (0 <= track <= 41):
            raise FloppyBuilderError("track number has to be between 0 and 41")
        if not (0 <= sector <= 41):
            raise FloppyBuilderError("sector number has to be between 0 and 41")
        self.current_track, self.current_sector = track, sector

    def cmd_write_sector(self, args):
        if len(args) != 1:
            raise FloppyBuilderError("syntax is 'WriteSector FilePath'")
        data = self._load_file(args[0])
        if len(data) > SECTOR_SIZE:
            raise FloppyBuilderError(f"'{args[0]}' is too large for a sector ({len(data)} bytes)")
        self.disk.write_sector_data(self.current_track, self.current_sector, data)
        if not self._advance_sector():
            raise FloppyBuilderError("disk is full")

    def cmd_write_loader(self, args):
        if len(args) != 2:
            raise FloppyBuilderError("syntax is 'WriteLoader FilePath LoadAddress'")
        if self.loader_load_address is not None:
            raise FloppyBuilderError("there can be only one loader on the disk")
        data = self._load_file(args[0])
        self.loader_load_address = convert_address(self._resolve(args[1]))
        for i in range(0, len(data), SECTOR_SIZE):
            self.disk.write_sector_data(self.current_track, self.current_sector,
                                         data[i:i + SECTOR_SIZE])
            if not self._advance_sector():
                raise FloppyBuilderError("disk is full")

    def _add_file_common(self, path: str, strip_tap_header: bool):
        data = self._load_file(path)
        if strip_tap_header:
            data = _strip_tap_header(data)
        if self.compression_mode != "None":
            raise FloppyBuilderError(
                "SetCompressionMode FilePack is not implemented in this tool "
                "(a deliberate scope cut, see docs/floppy.md) -- use 'SetCompressionMode None'"
            )
        disk_offset = self.disk.sector_disk_offset(self.current_track, self.current_sector)
        entry = FileEntry(path, self.current_track, self.current_sector, len(data), disk_offset)
        remaining = data
        while True:
            chunk, remaining = remaining[:SECTOR_SIZE], remaining[SECTOR_SIZE:]
            self.disk.write_sector_data(self.current_track, self.current_sector, chunk)
            if not remaining:
                self._advance_sector()
                break
            if not self._advance_sector():
                raise FloppyBuilderError(f"disk is full, can't fit all of '{path}'")
        self.file_entries.append(entry)

    def cmd_add_file(self, args):
        if len(args) != 1:
            raise FloppyBuilderError("syntax is 'AddFile FilePath'")
        self._add_file_common(args[0], strip_tap_header=False)

    def cmd_add_tap_file(self, args):
        if len(args) != 1:
            raise FloppyBuilderError("syntax is 'AddTapFile FilePath'")
        self._add_file_common(args[0], strip_tap_header=True)

    def cmd_reserve_sectors(self, args):
        if len(args) not in (1, 2):
            raise FloppyBuilderError("syntax is 'ReserveSectors SectorCount [FillValue]'")
        count = convert_address(self._resolve(args[0]))
        fill = convert_address(self._resolve(args[1])) if len(args) == 2 else 0
        disk_offset = self.disk.sector_disk_offset(self.current_track, self.current_sector)
        entry = FileEntry("Reserved sectors", self.current_track, self.current_sector,
                           count * SECTOR_SIZE, disk_offset)
        for _ in range(count):
            self.disk.write_sector_data(self.current_track, self.current_sector,
                                         bytes([fill]) * SECTOR_SIZE)
            if not self._advance_sector():
                raise FloppyBuilderError("disk is full")
        self.file_entries.append(entry)

    def cmd_add_define(self, args):
        if len(args) != 2:
            raise FloppyBuilderError("syntax is 'AddDefine Name Value'")
        self._add_define_for_last_file(self._resolve(args[0]), self._resolve(args[1]))

    def cmd_set_compression_mode(self, args):
        if len(args) != 1 or args[0] not in ("None", "FilePack"):
            raise FloppyBuilderError("syntax is 'SetCompressionMode [None|FilePack]'")
        self.compression_mode = args[0]

    def cmd_save_file(self, args):
        if len(args) != 4:
            raise FloppyBuilderError("syntax is 'SaveFile FilePath Track Sector SectorCount'")
        path = self._resolve(args[0])
        track = convert_address(self._resolve(args[1]))
        sector = convert_address(self._resolve(args[2]))
        count = convert_address(self._resolve(args[3]))
        self.extract_requests.append((path, track, sector, count))

    # -- header generation --------------------------------------------

    def generate_header(self) -> str:
        lines = [
            "// Generated by tools/oric_floppybuilder.py -- do not edit.",
            "// FloppyBuilder-script-compatible layout, C-only (no xa65 dual-format",
            "// section -- see docs/floppy.md).",
            "",
            f"#define FLOPPY_SIDE_NUMBER        {self.disk.sides}",
            f"#define FLOPPY_TRACK_NUMBER       {self.disk.tracks}",
            f"#define FLOPPY_SECTOR_PER_TRACK   {self.disk.sectors}",
        ]
        if self.loader_load_address is not None:
            lines.append(f"#define FLOPPY_LOADER_ADDRESS     {self.loader_load_address}")
        lines.append(f"#define FLOPPY_FILE_COUNT {len(self.file_entries)}")
        lines.append("")
        for name, value in self.define_list:
            lines.append(f"#define {name} {value}")
        if self.define_list:
            lines.append("")
        if self.file_entries:
            sectors = ", ".join(str(e.sector_field()) for e in self.file_entries)
            tracks = []
            for e in self.file_entries:
                if e.start_track < self.disk.tracks:
                    tracks.append(str(e.start_track))
                else:
                    tracks.append(str(e.start_track - self.disk.tracks + 128))
            sizes = ", ".join(str(e.size) for e in self.file_entries)
            lines.append(f"static const uint8_t  FloppyFileStartSector[FLOPPY_FILE_COUNT] = {{ {sectors} }};")
            lines.append(f"static const uint8_t  FloppyFileStartTrack [FLOPPY_FILE_COUNT] = {{ {', '.join(tracks)} }};")
            lines.append(f"static const uint16_t FloppyFileSize       [FLOPPY_FILE_COUNT] = {{ {sizes} }};")
            lines.append("")
            for i, e in enumerate(self.file_entries):
                lines.append(f"// Entry #{i} '{e.path}' -- track {e.start_track} sector "
                             f"{e.start_sector}, {e.size} bytes.")
        return "\n".join(lines) + "\n"


def _strip_tap_header(data: bytes) -> bytes:
    """Strips an Oric .tap tape header if present (AddTapFile), matching
    the reference's TapeInfo::ParseHeader -- see tools/mktap.py for this
    project's own writer of that same header format."""
    if len(data) >= 3 and data[0:3] == b"\x16\x16\x16":
        # Sync bytes + header marker at offset 3, then flags/addresses,
        # then a null-terminated filename starting at offset 13 (see
        # tools/mktap.py's header layout comment) -- data starts right
        # after the filename's null terminator.
        idx = data.index(b"\x00", 13) + 1
        return data[idx:]
    return data


COMMANDS = {
    "DefineDisk": "cmd_define_disk",
    "SetPosition": "cmd_set_position",
    "WriteSector": "cmd_write_sector",
    "WriteLoader": "cmd_write_loader",
    "AddFile": "cmd_add_file",
    "AddTapFile": "cmd_add_tap_file",
    "ReserveSectors": "cmd_reserve_sectors",
    "AddDefine": "cmd_add_define",
    "SetCompressionMode": "cmd_set_compression_mode",
    "SaveFile": "cmd_save_file",
}


def run_script(mode: str, script_path: str, defines: dict) -> Builder:
    builder = Builder(mode, defines)
    builder.script_dir = Path(script_path).parent or Path(".")
    format_version_seen = False
    output_layout_file = None
    output_floppy_file = None

    text = Path(script_path).read_text()
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        tokens, metadata = tokenize_line(raw_line)
        if not tokens:
            continue

        if tokens[0] == "FormatVersion":
            if len(tokens) != 2:
                raise FloppyBuilderError(f"line {line_number}: syntax is 'FormatVersion major.minor'")
            major_s, _, minor_s = tokens[1].partition(".")
            if tokens[1] == "0.19":
                raise FloppyBuilderError("AddFile does not have a size parameter anymore")
            format_version_seen = True
            continue

        if not format_version_seen:
            raise FloppyBuilderError("a 'FormatVersion major.minor' instruction is required first")

        if tokens[0] == "LoadDiskTemplate":
            raise FloppyBuilderError(
                "LoadDiskTemplate is not implemented in this tool (fresh-disk builds "
                "only, a deliberate scope cut -- see docs/floppy.md)"
            )
        if tokens[0] == "OutputLayoutFile":
            output_layout_file = builder._resolve(tokens[1])
            continue
        if tokens[0] == "OutputFloppyFile":
            output_floppy_file = builder._resolve(tokens[1])
            continue

        method_name = COMMANDS.get(tokens[0])
        if method_name is None:
            raise FloppyBuilderError(f"line {line_number}: unknown keyword '{tokens[0]}'")
        try:
            getattr(builder, method_name)([builder._resolve(t) for t in tokens[1:]])
        except FloppyBuilderError as e:
            raise FloppyBuilderError(f"line {line_number}: {e}") from e

    # Output/extract paths, like AddFile's input paths, are resolved
    # relative to the script's own directory -- matching the reference
    # tool's DirectoryChanger (it changes into the script's directory for
    # the whole run), not the caller's current working directory.
    if not builder.extract:
        if output_layout_file:
            (builder.script_dir / output_layout_file).write_text(builder.generate_header())
        if output_floppy_file:
            (builder.script_dir / output_floppy_file).write_bytes(bytes(builder.disk.buffer))
    else:
        for out_path, track, sector, count in builder.extract_requests:
            out = bytearray()
            for i in range(count):
                s = sector + i
                t = track
                while s > builder.disk.sectors:
                    s -= builder.disk.sectors
                    t += 1
                out.extend(builder.disk.read_sector_data(t, s))
            (builder.script_dir / out_path).write_bytes(bytes(out))

    return builder


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["init", "build", "extract"])
    ap.add_argument("script", help="description script path")
    ap.add_argument("-D", dest="defines", action="append", default=[],
                     metavar="name=value", help="add a define, e.g. -DDEMO_BIN=build/demo.bin")
    args = ap.parse_args()

    defines = {}
    for d in args.defines:
        name, _, value = d.partition("=")
        defines[name] = value

    try:
        builder = run_script(args.mode, args.script, defines)
    except FloppyBuilderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    if not builder.extract:
        print(f"Successfully created disk image ({len(builder.file_entries)} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
