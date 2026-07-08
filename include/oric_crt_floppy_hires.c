// oric_crt_floppy_hires.c - Oscar64 custom runtime for the floppy-disk
// build target's HIRES-mode content (see docs/floppy.md, docs/hires.md).
//
// A fourth runtime, needed because the other three are each missing one
// half of what a HIRES-mode floppy-disk demo needs:
//   - oric_crt.c / oric_crt_floppy.c: both use the full ~42.4KB main region
//     ($0580-$B1FF or $0580-$B200), which directly overlaps where the
//     HIRES bitmap must live ($A000-$BF3F).
//   - oric_crt_hires.c: shrinks the main region correctly for HIRES, but is
//     entered via tape auto-run, not tools/floppy/loader.c's boot handoff,
//     and doesn't know about that target's $FA00-$FFFF off-limits zone.
//
// This file merges the two: oric_crt_hires.c's region layout (verbatim)
// plus oric_crt_floppy.c's entry/startup context. The merge is a plain
// combination, not a redesign -- the HIRES reservation ($9800-$BFDF) and
// the floppy loader's reservation ($FA00-$FFFF) don't overlap each other,
// and the startup ASM sequence is identical across all four runtimes
// (SEI, clear BSS, clear ZP register file, set up software/hardware stack,
// JSR main, spin forever -- see oric_crt.c's own comment for the full
// rationale, not repeated here).
//
// Memory layout (compare with oric_crt_hires.c's and oric_crt_floppy.c's
// own tables):
//   $0000-$00FF  Zero page (Oscar64 internal registers)
//   $0100-$01FF  6502 hardware stack
//   $0200-$04FF  Oric system-variable page (ROM is gone on this target, but
//                nothing here reclaims this range either -- matches both
//                sibling runtimes)
//   $0500-$057F  Startup region (loader.c's boot handoff -> oric_startup;
//                same DEMO_ADDRESS convention as oric_crt_floppy.c)
//   $0580-$9600  Program code, data, BSS (~36.1KB -- same budget as
//                oric_crt_hires.c, for the same reason: HIRES mode needs
//                $9800-$BFDF, see below)
//   $9600-$97FF  6502 software stack (512 bytes)
//   $9800-$9BFF  HIRES-mode standard charset bank (HIRES_CHARSET_STD)
//   $9C00-$9FFF  HIRES-mode alternate charset bank (HIRES_CHARSET_ALT)
//   $A000-$BF3F  HIRES bitmap (HIRESVRAM, 8000 bytes)
//   $BF40-$BF67  Unused (42 bytes)
//   $BF68-$BFDF  Built-in 3-line TEXT footer (HIRES_FOOTER)
//   $BFE0-$F9FF  Unclaimed by any region (ROM is gone, but this target's
//                MVP doesn't reclaim it -- matches oric_crt_floppy.c's own
//                existing convention for this range)
//   $FA00-$FFFF  OFF LIMITS -- tools/floppy/loader.c's resident code and
//                its fixed API/vector block ($FFEF-$FFFF) live here for
//                the demo's ENTIRE runtime (every floppy_load() call does
//                `jsr $FFF7` into this range). Never place any region
//                here. Well clear of the HIRES reservation above, so
//                nothing here conflicts with it.
//
// $9800-$BFDF is deliberately left uncovered by any #pragma region below,
// exactly as oric_crt_hires.c does -- so the linker never places code/
// data/stack there.
//
// IRQ NOTE: same as the other three runtimes -- interrupts stay disabled
// (SEI; no CLI). loader.c has already bridged $0245/$0246 to real hardware
// vectors by the time our code runs (see oric_crt_floppy.c's own note) --
// rasterirq.c's hrirq_start() remains the only thing that enables them.

#include <crt.h>
#include <stdint.h>

// Oscar64 ZP register aliases (from crt.h / crt.c)
#define ip      __ip
#define sp      __sp
#define fp      __fp
#define accu    __accu
#define addr    __addr
#define tmp     __tmp
#define tmpy    __tmpy

// Linker-provided section boundary symbols
void StackStart, StackEnd, BSSStart, BSSEnd, CodeStart, CodeEnd, ZeroStart, ZeroEnd;

// -------------------------------------------------------------------------
// Section and region declarations -- identical to oric_crt_hires.c's own
// (see that file for why these specific boundaries).
// -------------------------------------------------------------------------

#pragma section(code,      0x0000, CodeStart,  CodeEnd)
#pragma section(stack,     0x0000, StackStart, StackEnd)
#pragma section(bss,       0x0000, BSSStart,   BSSEnd)
#pragma section(zeropage,  0x0000, ZeroStart,  ZeroEnd)

// Stack: 512 bytes, ending just below the relocated HIRES charset banks.
#pragma stacksize(0x0200)
#pragma region(stack, 0x9600, 0x9800, , , {stack})

// Startup region: loader.c's boot-handoff jump target (DEMO_ADDRESS),
// same convention as oric_crt_floppy.c.
#pragma region(startup, 0x0500, 0x0580, , , {})

// Main program region: shrunk to end at $9600 (HIRES budget, not the
// floppy target's usual $B200) so code/data/bss never overlaps the HIRES
// charset banks or bitmap. 'heap' is dropped: crt_math.c's crt_malloc
// always returns NULL, so no real heap is ever used.
#pragma region(main, 0x0580, 0x9600, , , {code, data, bss})

// -------------------------------------------------------------------------
// Startup -- identical sequence to the other three runtimes. Entered via
// loader.c's `jmp [DEMO_ADDRESS]` (a plain jump, not a call -- no pending
// return address to preserve), same as oric_crt_floppy.c.
// -------------------------------------------------------------------------

int main(void);

__asm oric_startup
{
    sei

    // Clear BSS — use 'ip' (Oscar64 ZP instruction pointer) as 16-bit pointer
    ldx     #>BSSStart
    ldy     #<BSSStart
    lda     #0
    sta     ip              // ip lo = 0
bss1:
    stx     ip + 1          // ip hi = current page
    cpx     #>BSSEnd
    beq     bss3            // last page: partial fill
bss2:
    sta     (ip), y
    iny
    bne     bss2
    inx
    bne     bss1
bss3:
    cpy     #<BSSEnd
    beq     bss_done
bss4:
    sta     (ip), y
    iny
    cpy     #<BSSEnd
    bne     bss4
bss_done:

    // Clear Oscar64 zero-page register file
    lda     #0
    ldx     #<ZeroStart
    bne     zp2
zp1:
    sta     $00, x
    inx
zp2:
    cpx     #<ZeroEnd
    bne     zp1

    // Set Oscar64 software stack pointer to top of stack region
    lda     #<StackEnd - 2
    sta     sp
    lda     #>StackEnd - 2
    sta     sp + 1

    // Initialise 6502 hardware stack pointer
    ldx     #$FF
    txs

    // Interrupts remain disabled (SEI above, no CLI here).

    // Call main (Oscar64 native mode: plain JSR)
    jsr     main

spexit:
    jmp     spexit      // spin on exit — no OS to return to
}

#pragma startup(oric_startup)

// Pull in Oscar64 integer and float runtime helpers (extracted from oscar64/include/crt.c).
// Required: Oscar64 always needs these runtime symbols even in native mode.
#pragma compile("crt_math.c")
