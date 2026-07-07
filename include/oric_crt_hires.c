// oric_crt_hires.c - Oscar64 custom runtime for Oric Atmos, HIRES-mode target
//
// Passed to Oscar64 via -rt=include/oric_crt_hires.c instead of the default
// include/oric_crt.c. Identical startup sequence to oric_crt.c -- the only
// difference is the 'main'/'stack' region layout, shrunk to leave room for
// the HIRES bitmap and its relocated charset banks. See include/hires.h.
//
// Memory layout:
//   $0000-$00FF  Zero page (Oscar64 internal registers)
//   $0100-$01FF  6502 hardware stack
//   $0200-$04FF  Oric ROM system variables (do not use)
//   $0500-$057F  Startup region (tape entry point -> oric_startup)
//   $0580-$95FF  Program code, data, BSS, heap (~36.1 KB -- down from ~42.4 KB
//                under oric_crt.c, since HIRES mode needs $9800-$BFDF, see below)
//   $9600-$97FF  6502 software stack (512 bytes)
//   $9800-$9BFF  HIRES-mode standard charset bank (HIRES_CHARSET_STD)
//   $9C00-$9FFF  HIRES-mode alternate charset bank (HIRES_CHARSET_ALT)
//   $A000-$BF3F  HIRES bitmap (HIRESVRAM, 8000 bytes)
//   $BF40-$BF67  Unused (42 bytes)
//   $BF68-$BFDF  Built-in 3-line TEXT footer (HIRES_FOOTER)
//   $C000-$FFFF  ROM (overlay RAM requires LOCI, see oric.h)
//
// $9800-$BFDF is deliberately left uncovered by any #pragma region below --
// exactly like oric_crt.c leaves TEXTVRAM/screen RAM uncovered today -- so
// the linker never places code/data/stack there; it's memory-mapped display
// RAM and relocated charset banks, not general-purpose RAM.
//
// IRQ NOTE: same as oric_crt.c -- interrupts stay disabled (SEI; no CLI).
// See that file's IRQ NOTE/CONVENTION comments for the full rationale.

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
// Section and region declarations
// -------------------------------------------------------------------------

#pragma section(code,      0x0000, CodeStart,  CodeEnd)
#pragma section(stack,     0x0000, StackStart, StackEnd)
#pragma section(bss,       0x0000, BSSStart,   BSSEnd)
#pragma section(zeropage,  0x0000, ZeroStart,  ZeroEnd)

// Stack: 512 bytes, moved down from oric_crt.c's $B200-$B400 to end just
// below the relocated HIRES charset banks at $9800.
#pragma stacksize(0x0200)
#pragma region(stack, 0x9600, 0x9800, , , {stack})

// Startup region: same as oric_crt.c.
#pragma region(startup, 0x0500, 0x0580, , , {})

// Main program region: shrunk to end at $9600 (was $B200 in oric_crt.c) so
// code/data/bss never overlaps the HIRES charset banks ($9800-$9FFF) or the
// HIRES bitmap ($A000-$BF3F). 'heap' is dropped for the same reason as
// oric_crt.c: crt_math.c's crt_malloc always returns NULL, so no real heap
// is ever used.
#pragma region(main, 0x0580, 0x9600, , , {code, data, bss})

// -------------------------------------------------------------------------
// Startup -- identical sequence to oric_crt.c. See that file for the
// step-by-step rationale (BSS clear, ZP clear, software/hardware stack init,
// call main, spin forever on return).
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
