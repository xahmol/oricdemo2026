#!/usr/bin/env python3
"""tests/scripts/test_floppybuilder.py

Pure-Python, no-emulator unit test for tools/oric_floppybuilder.py (the
`make test-floppybuilder` target -- see Makefile). Matches this repo's
existing "no test framework, standalone pass/fail-counting scripts"
convention (tests/scripts/test_pictconv.py).

Runs a mix of direct-function tests (CRC-16, gap-table/interleave math --
values hand-verified against Floppy.cpp's own crctab[]/CreateDisk() logic
when this test was written) and end-to-end script-driven tests (a small
fixture script + tiny fixture files -> exact byte-layout assertions on the
produced .dsk and generated header, run in a temp directory so nothing is
written into the repo).
"""

import struct
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))

import oric_floppybuilder as fb  # noqa: E402

pass_count = 0
fail_count = 0


def check(label, condition, detail=""):
    global pass_count, fail_count
    if condition:
        print(f"  [PASS] {label}")
        pass_count += 1
    else:
        print(f"  [FAIL] {label}" + (f" -- {detail}" if detail else ""))
        fail_count += 1


def check_raises(label, fn, exc_type=fb.FloppyBuilderError):
    try:
        fn()
        check(label, False, "expected an exception, none raised")
    except exc_type:
        check(label, True)
    except Exception as e:  # noqa: BLE001
        check(label, False, f"expected {exc_type.__name__}, got {type(e).__name__}: {e}")


def test_crc16():
    # Known CRC-16/CCITT (poly 0x1021, init 0xFFFF) test vector for the
    # ASCII string "123456789" is 0x29B1 -- a standard, widely-published
    # check value for this exact CRC variant (independent of this tool or
    # the reference FloppyBuilder), used here purely to confirm the table
    # generator/update loop are implemented correctly.
    result = fb.compute_crc(b"123456789")
    check("CRC-16/CCITT known test vector (123456789 -> 0x29B1)",
          result == bytes([0x29, 0xB1]), f"got {result.hex()}")


def test_define_disk_validation():
    check_raises("DefineDisk rejects sides != 2", lambda: fb.Disk(1, 42, 17))
    check_raises("DefineDisk rejects interleave >= sectors", lambda: fb.Disk(2, 42, 17, 17))
    check_raises("DefineDisk rejects interleave < 1", lambda: fb.Disk(2, 42, 17, 0))
    check_raises("DefineDisk rejects unsupported sector count", lambda: fb.Disk(2, 42, 20))
    check_raises("DefineDisk rejects out-of-range track count", lambda: fb.Disk(2, 10, 17))

    d = fb.Disk(2, 42, 17, 1)
    check("DefineDisk accepts a valid 2/42/17/1 disk", d is not None)
    check("Disk buffer size matches header + sides*tracks*6400",
          len(d.buffer) == 256 + 2 * 42 * 6400,
          f"got {len(d.buffer)}")


def test_disk_header_bytes():
    d = fb.Disk(2, 42, 17)
    check("Disk header signature is 'MFM_DISK'", bytes(d.buffer[0:8]) == b"MFM_DISK")
    check("Disk header sides field (LE32)", struct.unpack("<I", d.buffer[8:12])[0] == 2)
    check("Disk header tracks field (LE32)", struct.unpack("<I", d.buffer[12:16])[0] == 42)
    check("Disk header geometry field (LE32) is 1", struct.unpack("<I", d.buffer[16:20])[0] == 1)
    check("Disk header padding is zero-filled", all(b == 0 for b in d.buffer[20:256]))


def test_track_layout():
    d = fb.Disk(2, 42, 17)
    track0 = d.buffer[256:256 + 6400]
    check("Track length is exactly 6400 bytes", len(track0) == 6400)
    # gap1=72 for 17 sectors/track -> 60 bytes of 0x4E, then 12 zero bytes,
    # then the first sector's "A1 A1 A1 FE" ID mark.
    check("Track starts with (gap1-12)=60 bytes of 0x4E",
          all(b == 0x4E for b in track0[0:60]))
    check("First sector's sync-zero run precedes its ID mark",
          bytes(track0[72:75]) == b"\xA1\xA1\xA1" and track0[75] == 0xFE,
          f"got {track0[72:76].hex()}")


def test_sector_interleave_and_offsets():
    order, offsets = fb.compute_sector_data_offsets(17, 72, 34, 50, 1)
    check("Interleave=1 places logical sectors 1..17 in order",
          order == list(range(1, 18)), f"got {order}")
    check("17 distinct sector data offsets computed",
          len(set(offsets)) == 17, f"got {len(set(offsets))} unique of {len(offsets)}")

    order2, _ = fb.compute_sector_data_offsets(17, 72, 34, 50, 2)
    check("Interleave=2 does not place sectors in plain logical order",
          order2 != list(range(1, 18)), f"got {order2}")


def test_end_to_end_script():
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "loader.bin").write_bytes(b"LOADERDATA12")
        (tmp_path / "demo.bin").write_bytes(b"DEMOBINARYCONTENT")
        script = tmp_path / "script.txt"
        script.write_text(
            "FormatVersion 1.0\n"
            "DefineDisk 2 42 17\n"
            "OutputLayoutFile floppy_directory.h\n"
            "OutputFloppyFile out.dsk\n"
            "SetCompressionMode None\n"
            "\n"
            "SetPosition 0 3\n"
            "WriteLoader loader.bin 0xFA00\n"
            "\n"
            "AddFile demo.bin\n"
            "AddDefine LOADER_DEMO_FILE {FileIndex}\n"
        )

        builder = fb.run_script("build", str(script), {})

        dsk = (tmp_path / "out.dsk").read_bytes()
        check("build mode produces a .dsk of the expected size",
              len(dsk) == 256 + 2 * 42 * 6400, f"got {len(dsk)}")

        loader_offset = 256 + builder.disk.sector_data_offset[3 - 1]
        check("Loader bytes readable back from sector 3, track 0",
              dsk[loader_offset:loader_offset + 12] == b"LOADERDATA12",
              f"got {dsk[loader_offset:loader_offset + 12]!r}")

        demo_offset = 256 + builder.disk.sector_data_offset[4 - 1]
        check("Demo file bytes readable back from sector 4, track 0",
              dsk[demo_offset:demo_offset + 17] == b"DEMOBINARYCONTENT",
              f"got {dsk[demo_offset:demo_offset + 17]!r}")

        header = (tmp_path / "floppy_directory.h").read_text()
        check("Generated header defines LOADER_DEMO_FILE 0",
              "#define LOADER_DEMO_FILE 0" in header, header)
        check("Generated header's FloppyFileSize matches demo.bin's real length",
              "{ 17 }" in header, header)
        check("Generated header's FloppyFileStartSector matches AddFile's placement",
              "{ 4 }" in header, header)


def test_init_tolerates_missing_files():
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        script = tmp_path / "script.txt"
        script.write_text(
            "FormatVersion 1.0\n"
            "DefineDisk 2 42 17\n"
            "OutputLayoutFile floppy_directory.h\n"
            "OutputFloppyFile out.dsk\n"
            "AddFile does_not_exist.bin\n"
            "AddDefine LOADER_MISSING_FILE {FileIndex}\n"
        )
        try:
            fb.run_script("init", str(script), {})
            check("init mode tolerates a missing AddFile target", True)
        except fb.FloppyBuilderError as e:
            check("init mode tolerates a missing AddFile target", False, str(e))

        script2 = tmp_path / "script2.txt"
        script2.write_text(
            "FormatVersion 1.0\n"
            "DefineDisk 2 42 17\n"
            "OutputLayoutFile floppy_directory2.h\n"
            "OutputFloppyFile out2.dsk\n"
            "AddFile does_not_exist.bin\n"
        )
        check_raises("build mode fails fatally on a missing AddFile target",
                      lambda: fb.run_script("build", str(script2), {}))


def test_compression_and_template_scope_cuts():
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "demo.bin").write_bytes(b"X")
        script = tmp_path / "script.txt"
        script.write_text(
            "FormatVersion 1.0\n"
            "DefineDisk 2 42 17\n"
            "OutputLayoutFile floppy_directory.h\n"
            "OutputFloppyFile out.dsk\n"
            "SetCompressionMode FilePack\n"
            "AddFile demo.bin\n"
        )
        check_raises("SetCompressionMode FilePack is a hard error (scope cut)",
                      lambda: fb.run_script("build", str(script), {}))

        script2 = tmp_path / "script2.txt"
        script2.write_text(
            "FormatVersion 1.0\n"
            "LoadDiskTemplate default.dsk\n"
        )
        check_raises("LoadDiskTemplate is a hard error (scope cut, fresh-disk only)",
                      lambda: fb.run_script("build", str(script2), {}))


def main():
    print("=" * 63)
    print("  oric_floppybuilder.py -- unit tests")
    print("=" * 63)
    print()

    test_crc16()
    test_define_disk_validation()
    test_disk_header_bytes()
    test_track_layout()
    test_sector_interleave_and_offsets()
    test_end_to_end_script()
    test_init_tolerates_missing_files()
    test_compression_and_template_scope_cuts()

    print()
    print("=" * 63)
    print(f"  Results: {pass_count} passed, {fail_count} failed")
    print("=" * 63)

    return 1 if fail_count else 0


if __name__ == "__main__":
    sys.exit(main())
