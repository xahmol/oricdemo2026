// rasterirq.c - see rasterirq.h for the full design rationale.

#include <stdint.h>
#include "oric.h"
#include "rasterirq.h"

typedef struct {
    uint16_t cycle_offset;
    RasterCallback cb;
} _HrirqEntry;

static _HrirqEntry _hrirq_table[HRIRQ_MAX_CALLBACKS];
static uint8_t _hrirq_count;

// __hwinterrupt: an Oscar64-generated hardware-IRQ entry (full register
// save, exits via RTI) -- see oscar64manual.md "Interrupt handlers". This
// is the ENTIRE handler: no ROM chaining, no __asm needed for entry/exit.
// The busy-wait below is NOT cycle-exact -- see hrirq_add()'s doc comment
// and docs/rasterirq.md for the measured per-iteration cost and the
// calibration procedure a real raster-split effect needs.
__hwinterrupt void _hrirq_handler(void)
{
    // Reading T1C-L (VIA.t1lo) clears Timer 1's IFR interrupt flag --
    // standard 6522 semantics. Volatile so the read isn't optimized away.
    volatile uint8_t ack = VIA.t1lo;
    (void)ack;

    for (uint8_t i = 0; i < _hrirq_count; i++)
    {
        for (uint16_t d = 0; d < _hrirq_table[i].cycle_offset; d++)
            ;
        if (_hrirq_table[i].cb)
            _hrirq_table[i].cb();
    }
}

void hrirq_init(void)
{
    _hrirq_count = 0;
    uint16_t addr = (uint16_t)_hrirq_handler;
    IRQ_VEC_LO = (uint8_t)(addr & 0xFF);
    IRQ_VEC_HI = (uint8_t)(addr >> 8);
}

uint8_t hrirq_add(uint16_t cycle_offset, RasterCallback cb)
{
    if (_hrirq_count >= HRIRQ_MAX_CALLBACKS)
        return 0;
    _hrirq_table[_hrirq_count].cycle_offset = cycle_offset;
    _hrirq_table[_hrirq_count].cb = cb;
    _hrirq_count++;
    return 1;
}

void hrirq_start(void)
{
    __asm { cli }
}

void hrirq_stop(void)
{
    __asm { sei }
}
