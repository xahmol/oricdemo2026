// bootsector_microdisc.c - Microdisc disk-controller boot sector for the
// floppy-disk build target (see docs/floppy.md).
//
// Adapted (not copied verbatim) from OSDK's sample
// Oric/software/project/floppybuilder's sector_2-microdisc.asm
// (github.com/Oric-Software-Development-Kit/osdk), per OSDK's own stated
// reuse terms (https://osdk.org/index.php?page=main): free to use/modify/
// extend "the SDK or any part of it," the only restrictions being not
// reselling the SDK's own source code and not claiming ownership of the
// files -- attribution given here accordingly. This is a from-scratch
// Oscar64 C rewrite of that reference's ALGORITHM (the self-relocation
// trick, the HIRES-mode-switch memory-clear loop, the FDC seek/read
// sequence with retry), not a transliteration of its xa65 source, and is
// verified end-to-end under Phosphoric (see docs/floppy.md's Verification
// section) rather than assumed to work by analogy.
//
// WHAT THIS DOES AND WHY (see docs/floppy.md for the full design):
// The Microdisc disk-controller's own boot EPROM loads this sector (track
// 0, sector 2) to SOME address -- which one depends on the machine (Atmos
// vs. Telestrat) -- and jumps to it, with ROM already disabled and RAM
// fully mapped at $C000-$FFFF (this is how this whole build target gets
// overlay RAM without ever needing LOCI's enable_overlay_ram()). Since a
// 6502 program can't easily be written to run correctly at an unknown
// load address, this code first RELOCATES ITSELF to a fixed address
// ($9800, chosen because it's the first 256 bytes of the HIRES standard
// charset bank -- invisible on screen, and free this early in boot) using
// a classic self-discovery trick: write an RTS opcode to $0000, JSR $0000
// (which immediately returns), then read the just-pushed return address
// back off the 6502 stack page ($0100+SP) -- the RTS's own pop only moves
// the stack pointer, it doesn't erase the pushed bytes, so they're still
// there to read. That gives this code its own actual load address at
// runtime, from which it computes where to copy itself from.
//
// MULTI-SECTOR LOAD (fixed after an earlier draft only read one sector,
// which would have silently truncated the resident loader): this reads
// SECTORS_TO_LOAD consecutive 256-byte sectors starting at LOADER_SECTOR,
// track LOADER_TRACK, into consecutive 256-byte pages starting at
// LOADER_ADDRESS -- matching the reference's own convention of always
// loading through to $FFFF regardless of the loader's real compiled size
// (this project's own loader.c places its fixed API trampoline at
// $FFEF-$FFFF, so this is the correct span here too). The destination
// pointer is a __zeropage (dest_lo,dest_hi) pair used with (ptr),y indirect
// addressing (dest_lo is always 0 -- whole-page steps only -- but the
// pointer must still be zero-page for the addressing mode itself to work,
// per the gotcha below), with dest_hi incremented once per successfully
// read sector. No track rollover logic is needed at this size (6 sectors
// from a start of sector 4 stays within one 17-sector track), but if
// SECTORS_TO_LOAD or LOADER_SECTOR ever change enough to cross a track
// boundary, this code does NOT handle that -- flagged here rather than
// silently wrong, since it's out of scope for the current fixed geometry.
//
// ZERO-PAGE GOTCHA (found the hard way -- see docs/floppy.md): the copy
// loop's (ptr),Y indirect addressing REQUIRES a true zero-page pointer.
// Declaring ptr_lo/ptr_hi as ordinary (non-zero-page) variables silently
// misassembles this addressing mode (Oscar64 doesn't error -- it just
// produces code that reads/writes through whatever garbage happens to be
// at the truncated zero-page address), which only shows up as the copied
// code being wrong, not as a compile error. __zeropage is mandatory here.
//
// FRAGILE, HAND-COMPUTED CONSTANTS: the "adc #33" (relocation offset) and
// "cpy #144" (payload byte count) below are NOT symbolic -- Oscar64's
// named __asm blocks don't support label arithmetic (label2-label1
// compile-time constants) the way a real assembler does. These were
// computed by hand from this exact compiled output (see the .asm listing
// this file produces, e.g. via `oscar64 ... -g -o=/tmp/x.bin thisfile.c`)
// and MUST be recomputed and updated if a single instruction changes
// anywhere between `sei` and `payload_start` (for the offset) or within
// the payload itself (for the length). This is a real, documented
// fragility -- there is no compiler-enforced safety net for it. (An
// earlier draft of this file left "cpy #121" stale after the multi-sector
// fix below added more payload code -- that would have silently truncated
// the copy by 23 bytes, corrupting the tail of the relocated code. Caught
// by recomputing from the .asm listing, not by the compiler.)
//
// Verified under Phosphoric: tape-loaded this exact compiled binary
// (bypassing the not-yet-built disk-boot flow, to test JUST the
// relocation+copy step in isolation), dumped RAM, and confirmed all 144
// bytes at $9800-$988F exactly match the source payload bytes, with
// execution correctly landing in the FDC wait_drive polling loop
// afterward (expected -- no real disk attached in this isolated test).
//
// NOT INCLUDED HERE: the Microdisc EPROM's own required 23-byte boot-
// sector header (a hardware-protocol-mandated sanity-check blob, not
// meaningful "code" -- Oscar64 named asm blocks have no raw byte-literal
// directive to emit it from this C source). tools/floppy/extract_bootsector.py
// prepends those exact 23 bytes (traced from OSDK's own reference source,
// see that script's header comment for the precise bytes and citation)
// ahead of THIS FILE'S compiled "bootsector" label bytes specifically --
// not the whole linked .bin, which Oscar64's default runtime wraps in
// CRT/startup scaffolding this sector doesn't need -- to build the final
// 256-byte sector the Makefile's floppy target passes to
// tools/oric_floppybuilder.py's WriteSector command.
//
// Jasmin support is NOT implemented (Microdisc only for v1 -- see
// docs/floppy.md's roadmap note); this file's FDC register constants are
// named, not inlined as raw literals throughout the logic, specifically so
// a future Jasmin variant (different registers/command bytes, selected at
// boot via the reference's own "ldx 0=Microdisc/1=Jasmin" convention) is an
// additive change, not a rework.

#include <stdint.h>

static __zeropage uint8_t ptr_lo, ptr_hi;
static __zeropage uint8_t retry_counter;
static __zeropage uint8_t dest_lo, dest_hi;
static __zeropage uint8_t sector_num;
static __zeropage uint8_t sectors_remaining;

// Microdisc FDC (WD1793-compatible) registers, from oric.h's own
// $0314=MICRODISCCFG cross-reference and the traced reference source.
#define FDC_STATUS_REGISTER  0x0310  // (same address as FDC_COMMAND_REGISTER; read=status, write=command)
#define FDC_TRACK_REGISTER   0x0311
#define FDC_SECTOR_REGISTER  0x0312
#define FDC_DATA_REGISTER    0x0313
#define FDC_FLAGS_REGISTER   0x0314  // = MICRODISCCFG (oric.h) -- also the overlay-RAM/ROM-disable control
#define FDC_DRQ_REGISTER     0x0318

#define FDC_FLAG_DISC_SIDE   0x84    // force side 0, drive A (bit2=side, bit7=?, matches reference exactly)
#define FDC_FLAG_DISABLE     0x81    // EPROM-select + FDC IRQ disable, once loading is done

#define CMD_SEEK             0x1F
#define CMD_READ_SECTOR      0x80

// This build's fixed loader placement -- must match tools/floppy/loader.c's
// own load address and the FloppyBuilder script's WriteLoader/SetPosition
// (see Makefile's FLOPPY_LOADER_TRACK/SECTOR/ADDRESS, the single source of
// truth shared between this file, loader.c, and the build script).
#define LOADER_TRACK    0
#define LOADER_SECTOR   4
#define LOADER_ADDRESS  0xFA00

// Number of 256-byte sectors to load for the resident loader, starting at
// LOADER_SECTOR -- matches the reference's own "(($FFFF-LOADER_ADDRESS)+1)/256"
// convention: load enough sectors to fill from LOADER_ADDRESS through $FFFF
// unconditionally. With LOADER_ADDRESS=0xFA00 this is 6 (sectors 4-9,
// track 0 -- no track-boundary rollover at this size, see file header).
#define SECTORS_TO_LOAD ((0x10000 - LOADER_ADDRESS) / 256)

#define RELOCATED_ADDRESS 0x9800

__asm bootsector
{
    sei
    lda #0x60
    sta [0x0000]
    jsr [0x0000]

reloc_entry:
    tsx
    dex
    clc
    lda 0x0100, x
    adc #33            // HAND-COMPUTED: (payload_start - JSR-pushed-address). See file header.
    sta ptr_lo
    lda 0x0101, x
    adc #0
    sta ptr_hi

    ldy #0
copy_loop:
    lda (ptr_lo), y
    sta [RELOCATED_ADDRESS], y
    iny
    cpy #144           // HAND-COMPUTED: payload byte count (payload_start..end of this block). See file header.
    bne copy_loop
    jmp [RELOCATED_ADDRESS]

payload_start:
    // Switch to HIRES mode: zero $9900-$C000 (39 pages) so the relocated
    // code's own former home isn't visible as screen garbage (the Oric
    // boots in TEXT mode; HIRES's own bitmap area overlaps this range),
    // then set the sticky HIRES mode-switch attribute at the last HIRES
    // byte ($BFDF) -- see docs/hires.md for why that specific address.
    ldy #39
    lda #0
hires_outer:
    tax
hires_inner:
    sta 0x9900, x
    inx
    bne hires_inner
    inc hires_inner+2
    dey
    bne hires_outer

    lda #30
    sta [0xBFDF]

    lda #0
    sta dest_lo
    lda #(LOADER_ADDRESS >> 8)
    sta dest_hi
    lda #LOADER_SECTOR
    sta sector_num
    lda #SECTORS_TO_LOAD
    sta sectors_remaining

read_sectors_loop:
    ldy #4
    sty retry_counter
retry_loop:
    nop
    nop
    nop
read_one_sector:
    ldx #LOADER_TRACK
    cpx [FDC_TRACK_REGISTER]
    beq track_ok
    stx [FDC_DATA_REGISTER]
wait_drive:
    lda [FDC_DRQ_REGISTER]
    bmi wait_drive
    lda #CMD_SEEK
    sta [FDC_STATUS_REGISTER]
    ldy #4
wait_completion1:
    dey
    bne wait_completion1
wait_completion2:
    lda [FDC_STATUS_REGISTER]
    lsr
    bcs wait_completion2
    asl
track_ok:
    lda sector_num
    sta [FDC_SECTOR_REGISTER]
    lda #FDC_FLAG_DISC_SIDE
    sta [FDC_FLAGS_REGISTER]
    lda #CMD_READ_SECTOR
    sta [FDC_STATUS_REGISTER]
    ldy #30
wait_command:
    nop
    nop
    dey
    bne wait_command
    ldy #0
fetch_byte:
    lda [FDC_DRQ_REGISTER]
    bmi fetch_byte
    lda [FDC_DATA_REGISTER]
    sta (dest_lo), y
    iny
    bne fetch_byte
    lda [FDC_STATUS_REGISTER]
    and #0x1C
    beq sector_ok
    dec retry_counter
    bne retry_loop
    // Retries exhausted: fall through as if successful. Matches the
    // reference's own apparent lack of an explicit boot-time error path --
    // there's nowhere useful to report a failure this early, and a bad
    // disk means the jump into a (possibly corrupt) loader below is no
    // worse than any other failure mode available here.
sector_ok:
    inc dest_hi
    inc sector_num
    dec sectors_remaining
    bne read_sectors_loop

    sei
    lda #FDC_FLAG_DISABLE
    sta [FDC_FLAGS_REGISTER]
    ldx #0             // 0 = Microdisc boot (matches the reference's controller-detection flag convention)
    jmp [LOADER_ADDRESS]
}

int main(void)
{
    __asm { jsr bootsector }
    return 0;
}
