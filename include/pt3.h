// pt3.h - PT3 (Vortex Tracker / ProTracker 3) music player for Oric Atmos
//
// Original Z80 PT3 replay algorithm by S.V.Bulba (c)2004,2007, with Ivan
// Roshin for the note/volume table generators. 6502 port for the Oric
// ("Vortex Tracker II v1.0 PT3 player for 6502") by ScalexTrixx (A.C),
// (c)2018, MIT-licensed, in 6502Nerd/dflat (Oric branch,
// Oric/software/project/pt3/ppt3.s). This module is a from-scratch C
// rewrite for Oscar64/bare-metal Oric: the module header layout, pattern
// command-byte dispatch ranges, the 16 special-command meanings, and the
// order-list/pattern-pointer derivation were traced directly and precisely
// from that source (not assumed); the note table uses the standard
// 12-tone-equal-temperament/AY-clock formula (see pt3.c) rather than
// replicating ppt3.s's own hand-optimized NoteTableCreator generator, and
// FIX16BITS's Oric-clock correction (289/512, i.e. ~0.564x -- consistent
// with an Oric AY clock roughly half its 1MHz CPU clock, vs. the ZX
// Spectrum's ~1.7734MHz reference the table assumes) was derived precisely
// from ppt3.s's own FIX16BITS routine, not guessed.
//
// SCOPE: notes, ornaments, samples (including sample-driven tone-delta
// accumulation), volume, noise period, envelope (period/shape, with the
// "only write the shape register when a command sets a new one" rule),
// tempo, per-channel row-hold, and all four effects (portamento,
// glissando, vibrato on/off, envelope-glide) are implemented. The effects
// use a standard, musically-correct design (slide from source note to
// target note, or forever with no target; on/off amplitude pulsing; a
// shared envelope-period sweep) rather than a bit-exact replica of
// ppt3.s's own CrTnSl/TnDelt/PrNote bookkeeping for these specifically --
// see docs/pt3.md's "Effects" section for why that turned out to be
// underdetermined from static reading alone.
//
// Ticks at 50Hz via include/rasterirq.h's hrirq_add() -- see pt3_tick()
// below. Loads tune data at runtime from LOCI mass storage (include/loci.h)
// -- degrades to "no music", not a crash, when loci_present() is false.

#ifndef PT3_H
#define PT3_H

#include <stdint.h>
#include <stdbool.h>

// Maximum PT3 module size this player can load (no heap -- a fixed static
// buffer). Real PT3 tunes are typically 1-4KB; adjust if a larger tune
// needs to fit, mindful of the overall RAM budget (see docs/hires.md's
// memory-layout section if built under the HIRES runtime).
#ifndef PT3_MAX_MODULE_SIZE
#define PT3_MAX_MODULE_SIZE 6144
#endif

// Loads a PT3 module from LOCI storage into the internal module buffer.
// Returns false (silent no-op -- no music, not a crash) if LOCI isn't
// present, the file doesn't exist, or it exceeds PT3_MAX_MODULE_SIZE.
bool pt3_load(const char *path);

// Resets playback to the start of the currently loaded module (order
// position, per-channel state, tempo counter). Call once after a
// successful pt3_load(), before starting playback.
void pt3_init(void);

// One playback tick: decodes any pattern rows that are due, steps
// ornaments/samples, and writes all 14 AY registers via ay_write(). Call
// at 50Hz via hrirq_add() -- MUST be __interrupt-qualified (see
// rasterirq.h's callback-safety note); this function already is.
__interrupt void pt3_tick(void);

// Silences all 3 channels (zero amplitude) without touching hrirq state --
// call hrirq_stop() separately if pt3_tick() should stop firing entirely.
void pt3_stop(void);

// Returns a pointer to the last-computed AY registers 0-13 (14 bytes) --
// the real AY-3-8912 chip's internal state can't be read back from a
// Phosphoric RAM dump (confirmed empirically -- reading its VIA/PCR
// interface registers back reads flat 0x00 regardless of what was
// written), so this shadow copy exists specifically to make pt3_tick()'s
// computed output testable. Not needed by normal playback code.
const uint8_t *pt3_debug_shadow(void);

#pragma compile("pt3.c")

#endif // PT3_H
