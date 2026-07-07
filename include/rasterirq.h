// rasterirq.h - Raster IRQ / mid-frame effects (colour splits, raster bars)
//
// Both of this project's Oscar64 runtimes (oric_crt.c, oric_crt_hires.c)
// leave interrupts permanently disabled (SEI, no CLI) -- see oric_crt.c's
// IRQ NOTE, which already anticipates this as the intended extension
// point ("a proper IRQ handler with full register save/restore could be
// installed later if needed"). This module IS that handler.
//
// Design facts, researched against real Oric documentation (OSDK articles
// ART11 "Performance Profiling", ART14 "Overlay Memory", and a
// defence-force.org forum thread on IRQ vector hooking) rather than
// invented:
//
//   - Installs at IRQ_VEC_LO/IRQ_VEC_HI (oric.h, $0245/$0246) -- the
//     standard Oric IRQ redirection vector.
//   - The handler is FULLY SELF-CONTAINED: it never falls through to the
//     ROM's own dispatcher ($EE22), unlike the generic IRQ-hooking pattern
//     found in Oric forum examples. oric_crt.c's IRQ NOTE documents the
//     stock ROM handler corrupts zero page/screen RAM in this bare-metal
//     context; ART14 independently confirms "the standard Oric IRQ"
//     depends on ROM code and breaks if ROM banking changes. This handler
//     is pure RAM code with no ROM calls, directly or transitively, so it
//     stays safe regardless of overlay-RAM state (see loci.h's
//     enable_overlay_ram()).
//   - Coexists with the "SEI forever" convention: hrirq_init() does NOT
//     enable interrupts -- only hrirq_start() does (a single CLI), so any
//     program that never calls it is behaviourally unchanged. Once
//     hrirq_start() IS active, any code touching VIA Port A (ijk.h,
//     enable_overlay_ram()) MUST use the existing PHP/SEI...PLP convention
//     (oric_crt.c's CONVENTION comment) -- today that's inert (IRQs are
//     always off), but it becomes a LIVE hazard the moment IRQs are
//     enabled.
//   - Cycle budget, from ART11: 19,968 cycles/frame, 64 cycles/scanline at
//     1MHz/50Hz. hrirq_add()'s cycle_offset is NOT cycle-exact by itself
//     (it drives a plain busy-wait loop, whose real per-iteration cost
//     depends on Oscar64's generated code -- see rasterirq.c's comment for
//     the measured value) -- real raster-line-accurate effects need
//     empirical calibration (Oricutron's F2/F9 cycle counter, or
//     Phosphoric's cycle-accurate --dump-ram-at/--screenshot-at, the same
//     technique test_boot.sh's SPLASH_CYCLES already uses) before trusting
//     a specific offset value.

#ifndef RASTERIRQ_H
#define RASTERIRQ_H

#include <stdint.h>

typedef void (*RasterCallback)(void);

// Installs the custom IRQ handler at IRQ_VEC_LO/IRQ_VEC_HI and clears the
// callback schedule. Does NOT enable interrupts -- call hrirq_start() for
// that. Call once before hrirq_add()/hrirq_start().
void hrirq_init(void);

// Schedules a callback to run during the IRQ handler, after a busy-wait of
// approximately cycle_offset "delay units" (see rasterirq.c) since the
// PREVIOUS scheduled callback fired (or since Timer 1's IRQ trigger, for
// the first call after hrirq_init()) -- NOT an absolute frame-relative
// offset. Up to HRIRQ_MAX_CALLBACKS (8) may be scheduled; extra calls are
// silently ignored (checked via the return value).
//
// cb MUST be declared __interrupt (saves Oscar64's ZP pseudo-register
// file -- see oscar64manual.md "Interrupt handlers"), since it's called
// from within _hrirq_handler's __hwinterrupt context, which could
// interrupt the main program mid-expression while it's using those same
// ZP slots. Oscar64 emits "warning 2005: Calling non interrupt safe
// function" at the indirect call site in rasterirq.c regardless of the
// target's qualifier -- this is EXPECTED: the compiler can't statically
// verify safety through a stored function pointer, only through a direct
// call to a named function. The burden is on the caller to qualify cb
// correctly, exactly as the example in this header does.
#define HRIRQ_MAX_CALLBACKS 8
uint8_t hrirq_add(uint16_t cycle_offset, RasterCallback cb);

// Enables interrupts (CLI) -- the installed handler becomes active and
// Timer 1 IRQs (100Hz, see oric.h's TIMER1_100HZ) now trigger it.
void hrirq_start(void);

// Disables interrupts (SEI) -- reverts to this project's default IRQ-free
// state. Safe to call even if hrirq_start() was never called.
void hrirq_stop(void);

// Example:
//
//   __interrupt void my_split(void)
//   {
//       hires_row_colors(100, A_FWWHITE, A_BGRED);
//   }
//
//   hrirq_init();
//   hrirq_add(2000, my_split);   // approximate delay units, calibrate empirically
//   hrirq_start();
//   // ... main loop / effect logic ...
//   hrirq_stop();                // before returning to a context that doesn't expect live IRQs

#pragma compile("rasterirq.c")

#endif // RASTERIRQ_H
