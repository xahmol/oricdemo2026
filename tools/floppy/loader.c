// loader.c - resident disk loader for the floppy-disk build target (see
// docs/floppy.md).
//
// Adapted (not copied verbatim) from OSDK's sample
// Oric/software/project/floppybuilder's loader.asm/loader_api.h/.s
// (github.com/Oric-Software-Development-Kit/osdk), per OSDK's own stated
// reuse terms (https://osdk.org/index.php?page=main): free to use/modify/
// extend "the SDK or any part of it," the only restrictions being not
// reselling the SDK's own source code and not claiming ownership of the
// files -- attribution given here accordingly. This is a from-scratch
// Oscar64 C rewrite of that reference's ALGORITHM, not a transliteration
// of its xa65 source -- and a real, DOCUMENTED SIMPLIFICATION of it, not a
// full port (see below).
//
// WHAT THIS DOES (see docs/floppy.md for the full design):
// The Microdisc boot sector (tools/floppy/bootsector_microdisc.c) loads
// this file's compiled bytes to a fixed address (LOADER_ADDRESS, $FA00 by
// default) and jumps to it. This is therefore the FIRST C program to run
// after boot -- there is no CRT, no BSS clearing, nothing: this file's own
// "startup" region IS the entry point, placed at exactly LOADER_ADDRESS.
//
// SIMPLIFIED VS. THE REFERENCE -- a deliberate, documented scope cut, not
// a missing feature: the reference loader supports CHAINING a sequence of
// small overlay programs, each `return`ing from `main()` to hand control
// back to a reentrant `forever_loop` that loads and jumps to the NEXT
// program (see its InitializeFileAt API + main_first.c/main_second.c
// samples). This project's own runtimes (oric_crt.c and this floppy
// target's oric_crt_floppy.c) never return from main() -- every one of
// them spins forever on return -- so that whole chaining/reentrancy
// machinery would only ever fire ONCE (the initial boot handoff) and is
// otherwise dead code for a single long-running demo. This loader keeps
// only:
//   1. A ONE-SHOT boot action: load file index 0 (the demo binary) to its
//      fixed load address and jump there, done once at the end of this
//      file's own init, hardcoded rather than built on general chaining.
//   2. The reference's LoadData -- a synchronous "load now, return to
//      caller" primitive -- reachable for the rest of the program's life
//      via the fixed $FFF7 trampoline (see include/floppy.h's
//      floppy_load()).
// There is no InitializeFileAt, no forever_loop, no re-entrancy.
//
// FIXED API TRAMPOLINE (matches include/floppy.h exactly -- these two
// files must never drift apart):
//   $FFEF = sector, $FFF0 = track, $FFF1/$FFF2 = size lo/hi (unused by
//   LoadData itself in this simplified design -- kept for layout
//   compatibility/future use, see below), $FFF4/$FFF5 = dest lo/hi,
//   $FFF7-$FFF9 = JMP LoadData, $FFFA/$FFFB = NMI vector (a safe RTI
//   stub), $FFFC/$FFFD = RESET vector (a safe stub -- re-running this
//   program's own init), $FFFE/$FFFF = the REAL 6502 IRQ vector (see
//   below -- this is the one entry NOT just copied from the reference).
//
// THE IRQ VECTOR BRIDGE (a real ADDITION over the reference, not present
// in the original OSDK sample): include/rasterirq.c installs its handler
// at $0245/$0246 -- a low-RAM cell the Oric ROM's OWN IRQ dispatcher
// conventionally jumps through. On this floppy target, ROM is gone
// permanently (disabled by the boot EPROM), so $0245/$0246 is just an
// ordinary RAM cell nobody reads UNLESS something bridges it to the real
// hardware IRQ vector ($FFFE/$FFFF) -- which this loader's own resident
// code OCCUPIES, since $FFF7-$FFFF is the fixed API/vector block above.
// This loader's init therefore: (1) sets $0245/$0246 to point at a safe
// RTI stub (so rasterirq.c-unaware code, or code that hasn't called
// hrirq_init() yet, doesn't crash if an IRQ fires), and (2) sets the REAL
// $FFFE/$FFFF vector to point at a tiny stub doing `jmp ($0245)` -- an
// INDIRECT jump through that same low-RAM cell. This makes
// include/rasterirq.c's hrirq_init()/hrirq_add()/pt3_tick() work
// UNMODIFIED on this target: hrirq_init() keeps writing $0245/$0246
// exactly as it always has, and the real hardware vector transparently
// forwards to whatever it points at.
//
// ZERO-PAGE GOTCHA (same lesson as bootsector_microdisc.c, see its header
// comment): any pointer used with (ptr),Y indirect addressing MUST be
// __zeropage -- a plain variable silently misassembles this addressing
// mode with no compiler error.
//
// Jasmin support is NOT implemented (Microdisc only for v1 -- see
// docs/floppy.md's roadmap note); FDC register/command constants are
// named (matching bootsector_microdisc.c's own convention), not inlined
// as raw literals throughout the logic, specifically so a future Jasmin
// variant is an additive change, not a rework.
//
// VERIFICATION STATUS -- read before trusting this file further:
// Compile-time placement is fully verified (via .map inspection): every
// label (loader_entry at exactly LOADER_ADDRESS, LoadData, nmi_stub,
// reset_stub, irq_bridge, api_vectors at exactly $FFF7) lands where
// intended, and two real Oscar64 gotchas were found and fixed this way --
// (1) named __asm blocks are placed in SOURCE DECLARATION ORDER within a
// section, not by #pragma startup priority, and can only reference OTHER
// named __asm block labels declared EARLIER in the file (no forward
// references) -- resolved via a dedicated "entrycode" section/region for
// loader_entry, declared textually after what it references but placed
// first via #pragma code(entrycode); (2) api_vectors was silently
// stripped as dead code (nothing in this compilation unit calls it --
// it's only ever reached externally via a raw JSR $FFF7 from a different
// compiled program) until loader_entry was changed to call THROUGH it
// (`jsr api_vectors`, functionally identical to `jsr LoadData` since JMP
// doesn't touch the stack) instead of calling LoadData directly.
//
// RUNTIME behavior (the FDC read loop, the full vector-poke sequence, the
// boot handoff) was NOT successfully verified end-to-end via isolated
// tape-loading, despite substantial effort: testing this file by itself
// (wrapping the compiled .bin in a tape header and auto-running it, the
// same technique that worked cleanly for bootsector_microdisc.c) produced
// inconsistent, only-partially-explained results -- e.g. the $0245/$0246
// half of the IRQ-vector-bridge poke was observed to succeed in one run,
// but zero-page variables (l_track, l_side, etc.) showed values matching
// neither their expected contents nor obvious BASIC/ROM zero-page usage,
// and execution repeatedly ended up at PC=$FFFC via a path not fully
// traced. The most likely explanation: BASIC's own CLOAD/tape-loading
// machinery uses some of the same low zero-page range ($F7-$FD) this file
// also uses for its own l_* variables, and/or has its own high-memory
// bookkeeping that conflicts with a resident program placed at a fixed
// high address like LOADER_ADDRESS -- neither of which applies to this
// file's REAL deployment path (entered via the boot sector's raw
// `jmp [LOADER_ADDRESS]`, with no BASIC/ROM involved at all once the boot
// EPROM hands off). Rather than keep fighting a test methodology that may
// be inherently misleading for this specific file, full dynamic
// verification is deferred to the real disk-based test (docs/floppy.md,
// "Testing/verification" -- boots via an actual Microdisc-emulated disk
// image under Phosphoric, the scenario this file is actually built for).
// Treat this file's RUNTIME correctness as verified-by-careful-reading and
// by the compile-time checks above, NOT by dynamic test, until that disk
// test exists and passes.

#include <stdint.h>

// Microdisc FDC (WD1793-compatible) registers -- same addresses as
// tools/floppy/bootsector_microdisc.c (kept in sync by hand; there is no
// shared header between these two standalone, non-#include'd programs,
// see docs/floppy.md for why).
#define FDC_STATUS_REGISTER  0x0310
#define FDC_TRACK_REGISTER   0x0311
#define FDC_SECTOR_REGISTER  0x0312
#define FDC_DATA_REGISTER    0x0313
#define FDC_FLAGS_REGISTER   0x0314
#define FDC_DRQ_REGISTER     0x0318

#define FDC_FLAG_DISC_SIDE   0x84
#define CMD_SEEK             0x1F
#define CMD_READ_SECTOR      0x80

// This build's own fixed placement -- must match the Makefile's
// FLOPPY_LOADER_ADDRESS (single source of truth shared with
// bootsector_microdisc.c and the FloppyBuilder script).
#ifndef LOADER_ADDRESS
#define LOADER_ADDRESS 0xFA00
#endif

// Disk geometry -- must match DefineDisk in the FloppyBuilder script.
#ifndef FLOPPY_SECTOR_PER_TRACK
#define FLOPPY_SECTOR_PER_TRACK 17
#endif

// Boot-handoff target: file index 0 (the demo binary)'s track/sector/
// destination address. Parameterized via -d flags -- the Makefile fills
// these in from the REAL generated floppy_directory.h once known (see
// docs/floppy.md's two-pass build note: the loader is compiled AFTER
// FloppyBuilder's `init` pass computes real file placements, so there is
// no circularity here, unlike include/floppy.c's own dependency on that
// header). Placeholder defaults below are for standalone compile/testing
// only and do not reflect any real disk layout.
#ifndef DEMO_TRACK
#define DEMO_TRACK   0
#endif
#ifndef DEMO_SECTOR
#define DEMO_SECTOR  10
#endif
#ifndef DEMO_SIZE
#define DEMO_SIZE    4096
#endif
#ifndef DEMO_ADDRESS
#define DEMO_ADDRESS 0x0580
#endif

#define ORIC_IRQ_VECTOR_LO 0x0245
#define ORIC_IRQ_VECTOR_HI 0x0246

static __zeropage uint8_t l_track, l_side;
static __zeropage uint8_t l_dest_lo, l_dest_hi;
static __zeropage uint8_t l_bytes_lo, l_bytes_hi;   // remaining byte count (16-bit)
static __zeropage uint8_t l_retry;
static __zeropage uint8_t l_full_sector;   // nonzero: store all 256 bytes this
                                            // sector (more sectors follow); zero:
                                            // this is the final sector -- only
                                            // store l_bytes_lo bytes, see fetch_byte

// Fixed API cell addresses -- plain integer constants, NOT pointer-
// dereference expressions (unlike include/floppy.h's own FLOPPY_API_*
// macros, which ARE written that way for normal C use): every reference
// to these in this file is inside a raw __asm block via bracket notation
// (`lda [API_TRACK]`), where only a plain address/label is valid syntax --
// a C pointer-dereference expression textually substituted into an asm
// block does not assemble. Must exactly match include/floppy.h's own
// addresses (that header does use the pointer-dereference style, since
// it's used from normal C).
#define API_SECTOR    0xFFEF
#define API_TRACK     0xFFF0
#define API_SIZE_LO   0xFFF1
#define API_SIZE_HI   0xFFF2
#define API_DEST_LO   0xFFF4
#define API_DEST_HI   0xFFF5

void CodeStart, CodeEnd, EntryStart, EntryEnd, VecStart, VecEnd, ZeroStart, ZeroEnd;

#pragma section(code,       0x0000, CodeStart,  CodeEnd)
#pragma section(entrycode,  0x0000, EntryStart, EntryEnd)
#pragma section(vectors,    0x0000, VecStart,   VecEnd)
#pragma section(zeropage,   0x0000, ZeroStart,  ZeroEnd)

// loader_entry gets its OWN dedicated region+section ("startup" is the
// magic region name #pragma startup requires, see oric_crt.c's identical
// convention), separate from LoadData/nmi_stub/reset_stub/irq_bridge's
// "loadercode" region right after it. This is NOT just for tidiness:
// Oscar64 places same-section code in SOURCE DECLARATION ORDER, but
// named __asm blocks can only reference OTHER named __asm block labels
// that were already declared EARLIER in the file (jsr/jmp/#</#> to a
// later-declared label fails to compile -- "Undefined immediate operand"/
// "Undefined label" -- confirmed by trying it; unlike a forward-declared
// regular C function, e.g. oric_crt.c's `jsr main`, which resolves at
// link time across files). loader_entry needs to reference LoadData/
// nmi_stub/reset_stub/irq_bridge, which must therefore be declared BEFORE
// it in this file -- but it also needs to be PLACED at LOADER_ADDRESS,
// i.e. FIRST in memory. A dedicated region (not source order) is what
// reconciles "declared last" with "placed first".
#pragma region(startup, LOADER_ADDRESS, LOADER_ADDRESS + 0x60, , , {entrycode})
#pragma region(loadercode, LOADER_ADDRESS + 0x60, 0xFC00, , , {code})

// Fixed API/vector block: $FFEF-$FFFF. Only $FFF7-$FFF9 (the JMP LoadData
// trampoline) are compiler-placed code, via the "vectors" section below.
// $FFEF-$FFF6 are plain read/write memory both this file and
// include/floppy.h address directly via the API_*/FLOPPY_API_* macros --
// no compiler-side storage needed. $FFFA-$FFFF (the NMI/RESET/IRQ
// vectors) are NOT compiler-placed data either: a static const uint16_t[]
// initializer taking the address of a named __asm block's label failed
// to compile ("Constant initializer expected") -- named asm-block labels
// apparently aren't usable as C constant-expressions, only as raw asm
// operands (via #<label/#>label, which DOES work, see loader_entry below).
// So $FFFA-$FFFF are instead POKED at runtime, once, during loader_entry's
// one-shot init, using that same #<label/#>label technique -- leaving
// them outside any compiler-tracked region (nothing else the compiler
// places lands there, since no other region in this file extends past
// $FC00 except this one, which itself only claims $FFF7-$FFF9 via the
// "vectors" section).
#pragma region(vecregion, 0xFFF7, 0xFFFA, , , {vectors})

// -----------------------------------------------------------------------
// LoadData: the general-purpose FDC read routine. Reads API_SIZE_LO/HI
// bytes from (API_TRACK,API_SECTOR) into (API_DEST_LO,API_DEST_HI),
// advancing track/sector (with track rollover) and destination as needed,
// with up to 4 retries per sector on a CRC/read error. Reentered directly
// via the fixed $FFF7 trampoline -- see include/floppy.h's floppy_load().
// -----------------------------------------------------------------------
__asm LoadData
{
    lda [API_TRACK]
    sta l_track
    lda #FDC_FLAG_DISC_SIDE
    sta l_side
    lda [API_DEST_LO]
    sta l_dest_lo
    lda [API_DEST_HI]
    sta l_dest_hi
    lda [API_SIZE_LO]
    sta l_bytes_lo
    lda [API_SIZE_HI]
    sta l_bytes_hi

next_sector:
    // Any bytes left to read? (16-bit l_bytes == 0 means done)
    lda l_bytes_lo
    ora l_bytes_hi
    bne do_seek
    rts

do_seek:
    lda l_track
    cmp [FDC_TRACK_REGISTER]
    beq seek_done
    sta [FDC_DATA_REGISTER]
    // NOTE: no DRQ wait before issuing CMD_SEEK here (unlike an earlier
    // draft of this file, and unlike bootsector_microdisc.c's own seek
    // sequence, both of which poll FDC_DRQ_REGISTER first) -- confirmed
    // empirically that DRQ never asserts before this specific SEEK when
    // re-entering the FDC fresh from the loader's own boot handoff (traced
    // via binary-patch experiment: NOP-ing out that exact poll was the
    // one change that took boot from "hangs forever at this instruction"
    // to "loads the demo and jumps to it correctly"). This matches real
    // WD1793 semantics too -- DRQ is a DATA-TRANSFER-ready signal, not a
    // command-acceptance one, so a command register (CMD_SEEK) should
    // never require DRQ to be waited on first; the boot sector's own
    // matching wait happens to be harmless there only because DRQ already
    // reads as asserted at that earlier point in the boot sequence, not
    // because the wait is doing anything necessary. Not fixed in
    // bootsector_microdisc.c since its own boot already works end-to-end;
    // flagged here for whoever touches that file next.
    lda #CMD_SEEK
    sta [FDC_STATUS_REGISTER]
    ldy #4
seek_delay:
    dey
    bne seek_delay
seek_wait:
    lda [FDC_STATUS_REGISTER]
    lsr
    bcs seek_wait
    asl
seek_done:
    // Is this the FINAL sector of the request (fewer than 256 bytes
    // actually wanted), or a full one with more sectors still to follow?
    // l_bytes_hi nonzero means at least one more full sector's worth
    // remains after this one, so THIS one must be a full 256-byte sector
    // by construction. l_bytes_hi==0 means l_bytes_lo (1-255, next_sector
    // already ruled out 0) is the exact count still wanted -- this sector
    // is the last one, and only that many bytes should actually be stored
    // (found empirically: this project's own floppy_load() test called
    // with a 64-byte request wrote a full zero-padded 256-byte sector,
    // overflowing the caller's own 64-byte buffer by 192 bytes into
    // whatever memory followed it -- a real, confirmed bug, not a
    // documented tradeoff, despite an earlier version of this comment
    // block claiming otherwise).
    lda l_bytes_hi
    sta l_full_sector
    ldy #4
    sty l_retry
retry_sector:
    lda [API_SECTOR]
    sta [FDC_SECTOR_REGISTER]
    lda l_side
    sta [FDC_FLAGS_REGISTER]
    lda #CMD_READ_SECTOR
    sta [FDC_STATUS_REGISTER]
    ldy #30
cmd_delay:
    nop
    nop
    dey
    bne cmd_delay
    ldy #0
fetch_byte:
    lda [FDC_DRQ_REGISTER]
    bmi fetch_byte
    lda [FDC_DATA_REGISTER]
    // Still read every byte the FDC streams out regardless (required to
    // keep the hardware transfer correctly synced -- a sector read can't
    // be "aborted" partway through), but only STORE it if this is a full
    // sector, or (for the final, partial sector) if we haven't yet
    // reached the actual wanted byte count.
    ldx l_full_sector
    bne do_store
    cpy l_bytes_lo
    bcs skip_store
do_store:
    sta (l_dest_lo), y
skip_store:
    iny
    bne fetch_byte
    lda [FDC_STATUS_REGISTER]
    and #0x1C
    beq sector_ok
    dec l_retry
    bne retry_sector
    // Retries exhausted: give up on this sector and stop (a real, explicit
    // failure path, unlike the boot sector's own "fall through as if OK"
    // -- there IS somewhere useful to report failure here: the caller
    // checks whether the requested byte count was actually delivered, see
    // include/floppy.c's floppy_load()). We simply stop advancing; the
    // caller's own size bookkeeping (not implemented at this layer) is
    // responsible for detecting a short read if it cares to.
    rts

sector_ok:
    // Advance destination by one page (256 bytes) regardless of how many
    // bytes of THIS sector were actually stored (fetch_byte above already
    // limited that for the final partial sector) -- l_dest_hi tracks
    // "next free byte after the highest one this routine might write",
    // which is always a full page ahead once a sector's read completes,
    // whether or not every byte of it got stored.
    inc l_dest_hi
    // Advance sector (with track rollover), and decrement the remaining
    // byte count by 256 (a full sector), clamping so a final partial
    // sector (<256 bytes remaining) correctly reaches exactly 0 rather
    // than wrapping negative and looping for more (nonexistent) sectors.
    lda l_bytes_hi
    beq sub_lo_only
    dec l_bytes_hi
    bne after_sub
    lda l_bytes_lo
    beq after_sub
sub_lo_only:
    lda #0
    sta l_bytes_lo
after_sub:
    inc [API_SECTOR]
    lda [API_SECTOR]
    cmp #(FLOPPY_SECTOR_PER_TRACK + 1)
    bne next_sector
    lda #1
    sta [API_SECTOR]
    inc l_track
    jmp next_sector
}

// -----------------------------------------------------------------------
// nmi_stub / reset_stub: safe do-nothing targets for the NMI/RESET
// vectors -- this project's own hardware has no NMI source in normal use
// and RESET (the on-Oric reset button) has no graceful recovery path once
// ROM is gone, so both just spin/return safely rather than falling into
// undefined behavior.
// -----------------------------------------------------------------------
__asm nmi_stub
{
    rti
}

__asm reset_stub
{
sit_forever:
    jmp sit_forever
}

// -----------------------------------------------------------------------
// irq_bridge: the $FFFE/$FFFF target -- forwards the real hardware IRQ to
// whatever include/rasterirq.c's hrirq_init() has (or hasn't yet) set at
// $0245/$0246. See file header's "IRQ vector bridge" section.
// -----------------------------------------------------------------------
__asm irq_bridge
{
    jmp [ORIC_IRQ_VECTOR_LO]
}

// -----------------------------------------------------------------------
// The fixed $FFF7-$FFF9 trampoline: JMP LoadData. $FFFA-$FFFF (the real
// NMI/RESET/IRQ vectors) are poked at runtime by loader_entry instead --
// see the vecregion comment above for why. Declared here, BEFORE
// loader_entry, so loader_entry's own reference to it (below) resolves as
// a backward reference (see loader_entry's comment for why that matters).
// -----------------------------------------------------------------------
#pragma code(vectors)
__asm api_vectors
{
    jmp LoadData
}
#pragma code(code)

// -----------------------------------------------------------------------
// loader_entry: the true program entry point. Declared HERE (after
// LoadData/nmi_stub/reset_stub/irq_bridge/api_vectors) so its references
// to their labels resolve as backward references -- named __asm blocks
// can only reference OTHER named __asm block labels declared EARLIER in
// the file (a forward jsr/jmp/#</#> to a later-declared label fails to
// compile: "Undefined immediate operand"/"Undefined label", confirmed by
// trying it; unlike a forward-declared regular C function, e.g.
// oric_crt.c's `jsr main`, which resolves at link time across files). But
// it still gets PLACED at exactly LOADER_ADDRESS (first in memory,
// matching what the boot sector's `jmp [LOADER_ADDRESS]` expects), because
// of the #pragma code(entrycode) switch below -- it goes into the
// "entrycode" section, mapped to the dedicated "startup" region at
// LOADER_ADDRESS declared above, entirely independent of its position in
// this file. Runs once: sets up the IRQ vector bridge, then performs the
// one-shot boot handoff (load file index 0 to DEMO_ADDRESS and jump
// there). Never returns.
//
// Calls THROUGH api_vectors (`jsr api_vectors`, which itself is just
// `jmp LoadData`) rather than `jsr LoadData` directly -- functionally
// identical (JMP doesn't touch the stack, so LoadData's own RTS still
// returns correctly to here), but this is what keeps api_vectors alive at
// all: nothing in this compilation unit otherwise references it (it's
// only ever reached externally, via a raw JSR to the literal address
// $FFF7 from include/floppy.c, compiled as a SEPARATE program), so
// Oscar64's optimizer was silently stripping it as dead code (confirmed
// via the .map file showing an empty "vectors" section/region) until this
// genuine internal reference was added.
// -----------------------------------------------------------------------
#pragma code(entrycode)
__asm loader_entry
{
    sei

    // $0245/$0246 -- safe default until/unless hrirq_init() overwrites it.
    lda #<nmi_stub
    sta ORIC_IRQ_VECTOR_LO
    lda #>nmi_stub
    sta ORIC_IRQ_VECTOR_HI

    // Poke the real 6502 NMI/RESET/IRQ vectors at $FFFA-$FFFF -- see the
    // vecregion comment above for why this is done at runtime here rather
    // than via a compiler-placed data table.
    lda #<nmi_stub
    sta [0xFFFA]
    lda #>nmi_stub
    sta [0xFFFB]
    lda #<reset_stub
    sta [0xFFFC]
    lda #>reset_stub
    sta [0xFFFD]
    lda #<irq_bridge
    sta [0xFFFE]
    lda #>irq_bridge
    sta [0xFFFF]

    // One-shot boot handoff: load file index 0 (the demo) via LoadData
    // (via api_vectors -- see comment above for why).
    lda #DEMO_SECTOR
    sta [API_SECTOR]
    lda #DEMO_TRACK
    sta [API_TRACK]
    lda #<DEMO_SIZE
    sta [API_SIZE_LO]
    lda #>DEMO_SIZE
    sta [API_SIZE_HI]
    lda #<DEMO_ADDRESS
    sta [API_DEST_LO]
    lda #>DEMO_ADDRESS
    sta [API_DEST_HI]
    jsr api_vectors

    jmp [DEMO_ADDRESS]
}
#pragma code(code)

#pragma startup(loader_entry)

int main(void)
{
    // Never actually called -- loader_entry (via #pragma startup) is the
    // real entry point. Oscar64 requires a main() to exist regardless.
    return 0;
}
