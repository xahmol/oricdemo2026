// oric_crt.c - Oscar64 custom runtime for Oric Atmos bare-metal target
//
// Passed to Oscar64 via -rt=include/oric_crt.c
// Provides: memory layout, startup sequence.
//
// Memory layout:
//   $0000-$00FF  Zero page (Oscar64 internal registers)
//   $0100-$01FF  6502 hardware stack
//   $0200-$04FF  Oric ROM system variables (do not use)
//   $0500-$057F  Startup region (tape entry point -> oric_startup)
//   $0580-$B1FF  Program code, data, BSS, heap (~42.4 KB)
//   $B200-$B3FF  6502 software stack (512 bytes)
//   $B400-$BBFF  Character set RAM (standard $B400-$B7FF, alternate $B800-$BBFF)
//                — left untouched by code/data/stack so charset glyphs used by
//                  the version splash are not corrupted by stack contents.
//                  $B400 is OSDK's documented start of charset RAM — the hard
//                  TEXT-mode ceiling for user code/data.
//   $BB80-$FFFF  Screen RAM ($BB80) + ROM ($C000)
//
// The overlay RAM at $C000-$FFFF requires LOCI device; not mapped as a code region.
//
// IRQ NOTE: Interrupts are left disabled (SEI; no CLI). The Oric ROM IRQ chain
// at $0245/$0246 has a complex stack protocol that is difficult to hook safely
// without detailed ROM source analysis. The keyboard scanner needs no IRQs: it
// polls directly via VIA/AY, and disabling IRQs eliminates the ROM cursor-blink
// artifact. A real handler could be installed once the ROM's stack convention
// is fully understood, or by using LOCI-provided IRQ infrastructure.
//
// CONVENTION: any code that must briefly enable IRQs (overlay RAM access via
// MICRODISCCFG, or VIA Port A access in ijk.c) MUST use PHP/SEI ... PLP, not
// SEI ... CLI. An unconditional CLI would permanently re-enable IRQs (since
// startup never re-enables them and no handler is installed), letting the
// stock ROM IRQ handler run every frame and corrupt zero page / screen RAM.
// PHP/PLP preserves whatever interrupt-disable state was in effect before.

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

// Stack: 512 bytes, just below the standard/alternate character set RAM
#pragma stacksize(0x0200)
#pragma region(stack, 0xB200, 0xB400, , , {stack})

// Startup region: Oscar64 requires a region named "startup" for the #pragma startup
// function. Without it Oscar64 auto-creates one at $0800, conflicting with main.
// We place it at $0500 so the tape entry point runs our oric_startup directly.
#pragma region(startup, 0x0500, 0x0580, , , {})

// Main program region: starts at $0580 (after startup region)
// 'heap' is dropped from the section list: crt_math.c's crt_malloc always
// returns NULL, so no real heap is ever used, and oscar64 errors "Cannot
// place heap section" if a non-empty heap section is requested but has
// no room left to place.
#pragma region(main, 0x0580, 0xB200, , , {code, data, bss})

// -------------------------------------------------------------------------
// Startup
//
// Sequence:
//   1. SEI — disable interrupts (kept disabled; keyboard scanner is pure poll)
//   2. Clear BSS (using Oscar64's 'ip' ZP register as pointer)
//   3. Clear Oscar64 ZP register file (ZeroStart..ZeroEnd)
//   4. Set Oscar64 software stack pointer (sp = StackEnd - 2)
//   5. Init 6502 hardware stack pointer to $FF
//   6. JSR main (native mode: direct call)
//   7. JMP spexit — spin forever on return (bare-metal, no OS)
//
// IRQ NOTE: We leave interrupts disabled (no CLI). The Oric ROM IRQ chain at
// $0245/$0246 uses a non-trivial stack protocol (ROM saves A/X/Y then does
// a vectored call; the exact return convention requires precise stack knowledge
// to avoid corruption). No interrupts are needed: keyboard is polled via
// direct VIA/AY access, charwin is synchronous, and disabling IRQs also
// eliminates the ROM cursor-blink artifact.
// A proper IRQ handler with full register save/restore could be installed
// later if needed.
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
    // Keyboard is polled, no IRQ handler needed.

    // Call main (Oscar64 native mode: plain JSR)
    jsr     main

spexit:
    jmp     spexit      // spin on exit — no OS to return to
}

#pragma startup(oric_startup)

// Pull in Oscar64 integer and float runtime helpers (extracted from oscar64/include/crt.c).
// Required: Oscar64 always needs these runtime symbols even in native mode.
#pragma compile("crt_math.c")
