// ay.c - see ay.h.

#include "ay.h"
#include "oric.h"

// PCR values (VIA.pcr = $030C), CB2/CA2 edge-select bits, driving the AY's
// BC1/BDIR bus-control inputs -- named to match 6502Nerd/dflat's ppt3.s.
#define SND_SELWRITE    0xFD    // fct: WRITE DATA
#define SND_SELSETADDR  0xFF    // fct: SET PSG REG#
#define SND_DESELECT    0xDD    // fct: INACTIVE

void ay_write(uint8_t reg, uint8_t value)
{
    // PHP/PLP, not SEI/CLI -- see ijk.c's identical convention: don't
    // assume/force the interrupt-enable state, just preserve whatever it
    // already was (this must stay safe to call both from normal code with
    // interrupts permanently disabled, and from __interrupt callback
    // context where they're already disabled by the caller).
    __asm { php }
    __asm { sei }

    VIA.pra2 = reg;
    VIA.pcr  = SND_SELSETADDR;
    VIA.pcr  = SND_DESELECT;

    VIA.pra2 = value;
    VIA.pcr  = SND_SELWRITE;
    VIA.pcr  = SND_DESELECT;

    __asm { plp }
}
