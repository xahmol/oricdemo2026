// oric_crt_floppy.c - Oscar64 custom runtime for the floppy-disk build
// target (see docs/floppy.md). Sibling to oric_crt.c (the tape/LOCI
// runtime) and oric_crt_hires.c (the HIRES runtime) -- mutually exclusive
// with BOTH of those for a given build, same as they are with each other.
//
// Passed to Oscar64 via -rt=include/oric_crt_floppy.c
// Provides: memory layout, startup sequence.
//
// HOW THIS DIFFERS FROM oric_crt.c, AND WHY IT'S MOSTLY THE SAME:
// On the tape/LOCI target, a program is entered via BASIC's own tape
// auto-run, which jumps to a fixed address with ROM still present higher
// up (LOCI's enable_overlay_ram() is what reclaims $C000-$FFFF there, and
// only while explicitly enabled). On THIS target, the whole demo binary
// is a separate compiled program, embedded into the disk image by
// tools/oric_floppybuilder.py and loaded by tools/floppy/loader.c's
// LoadData, then entered via a plain `jmp [DEMO_ADDRESS]` -- by which
// point the Microdisc boot EPROM has ALREADY disabled ROM permanently and
// mapped RAM across the whole $C000-$FFFF range, and loader.c's own
// loader_entry has ALREADY set up the $0245/$0246-to-real-hardware-vector
// IRQ bridge (see loader.c's own header comment) and poked safe NMI/RESET
// stubs at $FFFA-$FFFD. None of that is this file's job to redo.
//
// Given that, this runtime's actual STARTUP WORK is nearly identical to
// oric_crt.c's: clear BSS, clear the Oscar64 ZP register file, set up the
// software stack pointer and the 6502 hardware stack pointer, JSR main,
// spin forever on return. The interrupt-disable convention (SEI, no CLI
// -- see oric_crt.c's own IRQ NOTE) is unchanged too: this target doesn't
// NEED interrupts enabled for keyboard/charwin/etc any more than the
// tape/LOCI target does -- rasterirq.c's hrirq_start() remains the only
// thing that enables them, on either target, and it works unmodified here
// specifically because of loader.c's IRQ vector bridge.
//
// MEMORY LAYOUT (compare with oric_crt.c's table):
//   $0000-$00FF  Zero page (Oscar64 internal registers)
//   $0100-$01FF  6502 hardware stack
//   $0200-$04FF  Oric system-variable page -- ROM is gone on this target,
//                but nothing here reclaims this range either (matches
//                oric_crt.c; not this MVP's scope, see docs/floppy.md)
//   $0500-$057F  Startup region (loader.c's boot handoff -> oric_startup)
//   $0580-$B1FF  Program code, data, BSS, heap (~42.4 KB -- same budget
//                as the tape/LOCI target; this MVP does NOT reclaim the
//                extra headroom this target's permanently-gone ROM could
//                in principle offer up to $F9FF, see docs/floppy.md's
//                "What this plan deliberately does not cover")
//   $B200-$B3FF  6502 software stack (512 bytes)
//   $B400-$BBFF  Character set RAM (same as oric_crt.c)
//   $BB80-$F9FF  Screen RAM ($BB80) + real RAM (ROM gone, but unclaimed
//                by any region here -- do not place code/data here without
//                first re-checking this comment and loader.c's own
//                placement, since nothing currently prevents Oscar64 from
//                doing so if a region were added carelessly)
//   $FA00-$FFFF  OFF LIMITS -- tools/floppy/loader.c's resident code and
//                its fixed API/vector block ($FFEF-$FFFF) live here for
//                the demo's ENTIRE runtime (not just at boot): every
//                floppy_load() call (include/floppy.c) does `jsr $FFF7`
//                into this range. Never place any region here.
//
// IRQ NOTE: see oric_crt.c's identical note -- it applies here unchanged.
// The one addition on this target is loader.c's IRQ vector bridge, not
// anything in this file.

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
// Section and region declarations -- identical boundaries to oric_crt.c
// (see the memory-layout comment above for why: this MVP doesn't reclaim
// the extra RAM this target's gone ROM could in principle offer, and the
// $FA00-$FFFF loader/API block is well clear of all of these regardless).
// -------------------------------------------------------------------------

#pragma section(code,      0x0000, CodeStart,  CodeEnd)
#pragma section(stack,     0x0000, StackStart, StackEnd)
#pragma section(bss,       0x0000, BSSStart,   BSSEnd)
#pragma section(zeropage,  0x0000, ZeroStart,  ZeroEnd)

// Stack: 512 bytes, just below character set RAM (same as oric_crt.c)
#pragma stacksize(0x0200)
#pragma region(stack, 0xB200, 0xB400, , , {stack})

// Startup region: Oscar64 requires a region named "startup" for the
// #pragma startup function (see oric_crt.c's identical comment). Placed
// at $0500 so the Makefile's floppy-target DEMO_ADDRESS (tools/floppy/
// loader.c's boot-handoff jump target) can point directly at it, matching
// the tape/LOCI target's own entry address for consistency (not a hard
// requirement -- DEMO_ADDRESS is just a build parameter -- but there is
// no reason for it to differ).
#pragma region(startup, 0x0500, 0x0580, , , {})

// Main program region: starts at $0580 (after startup region). 'heap' is
// dropped from the section list for the same reason as oric_crt.c: no
// real heap is ever used (crt_math.c's crt_malloc always returns NULL).
#pragma region(main, 0x0580, 0xB200, , , {code, data, bss})

// -------------------------------------------------------------------------
// Startup
//
// Sequence (identical to oric_crt.c's -- see that file's own comment for
// the full rationale): SEI, clear BSS, clear Oscar64 ZP register file,
// set Oscar64 software stack pointer, init 6502 hardware stack pointer,
// JSR main, spin forever on return. Entered via loader.c's
// `jmp [DEMO_ADDRESS]` (a plain jump, not a call -- there is no pending
// return address to preserve or disturb by resetting the hardware stack
// pointer here).
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

    // Interrupts remain disabled (SEI above, no CLI here) -- see the file
    // header's IRQ NOTE.

    // Call main (Oscar64 native mode: plain JSR)
    jsr     main

spexit:
    jmp     spexit      // spin on exit — no OS to return to
}

#pragma startup(oric_startup)

// Pull in Oscar64 integer and float runtime helpers (same as oric_crt.c).
#pragma compile("crt_math.c")
