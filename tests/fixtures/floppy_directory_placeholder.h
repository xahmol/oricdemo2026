// floppy_directory_placeholder.h - checked-in stand-in for the
// FloppyBuilder-generated build/floppy_directory.h (see docs/floppy.md's
// two-pass build note).
//
// Used ONLY for the floppy demo's first compile pass, whose sole purpose
// is to learn the demo binary's real compiled SIZE (needed by
// tools/oric_floppybuilder.py's `init` pass to place files and compute
// real track/sector/size values) -- the actual VALUES here are never used
// for anything. Same array shapes as the real generated header (same
// FLOPPY_FILE_COUNT as src/floppy_test.c's own file-index convention: 0 =
// itself, 1 = test payload, 2 = tests/fixtures/music.pt3), so the compile
// succeeds identically to how it will with the real header in pass two.

#define FLOPPY_FILE_COUNT 3
static const uint8_t  FloppyFileStartSector[FLOPPY_FILE_COUNT] = { 0, 0, 0 };
static const uint8_t  FloppyFileStartTrack [FLOPPY_FILE_COUNT] = { 0, 0, 0 };
static const uint16_t FloppyFileSize       [FLOPPY_FILE_COUNT] = { 0, 64, 600 };
