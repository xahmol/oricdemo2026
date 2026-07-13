// arkos.h - Arkos Tracker (.aky) music player for Oric Atmos
//
// Original AKY music player - V1.0, by Julien Nevo a.k.a. Targhan/Arkos
// (with CPC PSG-sending optimizations by Madram/Overlanders), December
// 2016. 6502 conversion (Apple II + Mockingboard | Oric 1/Atmos) by Arnaud
// Cocquiere a.k.a GROUiK/FRENCH TOUCH. OSDK/Oric sample program adaptation
// by Mickael Pointier (Dbug), github.com/Oric-Software-Development-Kit/
// Arkos-Music-Player (`akyplayer.s`). This module is a from-scratch C
// rewrite for Oscar64/bare-metal Oric -- see docs/arkos.md for exactly
// which parts were precisely traced from that reference vs. simplified,
// and for the full format writeup (Linker/Track/RegisterBlock layout).
//
// Replaces this project's earlier PT3 (Vortex Tracker) player, archived on
// the `pt3` branch after several rounds of decode-bug fixes still didn't
// produce music the user found satisfying, and its runtime overhead was
// judged too high. Arkos's own RegisterBlock format is closer to
// "pre-rendered register writes with light per-frame decode" than PT3's
// live note/effect decode, which is expected to be both faster and (once
// verified) more faithful to the original composition.
//
// MEMORY: unlike PT3 (which loaded into an ordinary linker-placed static
// buffer), the .aky format bakes ABSOLUTE 16-bit pointers into the file at
// export time (Arkos Tracker's own "Encode to address" export setting) --
// there is no relocation in this player, so the module MUST be loaded at
// the exact address it was exported for. This project's own convention:
// every .aky file must be exported to $C000 (ARKOS_MODULE below), matching
// this project's overlay-RAM buffer -- see docs/arkos.md and both HIRES
// runtimes' own memory-layout comments (include/oric_crt_hires.c,
// include/oric_crt_floppy_hires.c) for why $C000 specifically (keeps the
// buffer out of the ~36KB main code/data/BSS budget entirely, using
// otherwise-unused overlay RAM/floppy-target RAM instead).

#ifndef ARKOS_H
#define ARKOS_H

#include <stdint.h>
#include <stdbool.h>

// Fixed load address -- see the file-level comment above. Every .aky file
// used by this project MUST be exported (Arkos Tracker's "Encode to
// address" setting) to exactly this address.
#define ARKOS_MODULE            0xC000U

// Maximum usable size: $C000-$F9FF (14848 bytes), NOT the full $C000-$FFFF
// overlay/RAM window -- $FA00-$FFFF is permanently reserved for the
// floppy-disk target's own resident loader (tools/floppy/loader.c) and its
// fixed API cells, on BOTH targets, for one shared file/size convention
// (the tape/LOCI target doesn't strictly need this reservation itself, but
// keeping one number avoids two different maximum sizes per target).
#define ARKOS_MAX_MODULE_SIZE   0x3A00U   // 14848 bytes

// Loads a .aky module. Two storage backends, selected at compile time
// (matching this project's existing -dSTORAGE_FLOPPY convention -- see
// docs/floppy.md): NOT alternative signatures for the same operation, a
// genuine difference between the two build targets.
//
// Tape/LOCI target (default): loads via include/loci.h's file_load().
// Real Atmos ROM normally occupies $C000-$FFFF on this target, so
// arkos_load() enables overlay RAM (oric.h's OVERLAY_ON, via loci.c's
// enable_overlay_ram()) BEFORE loading, and leaves it enabled for the
// rest of the program's runtime (see docs/arkos.md's "Overlay RAM" section
// for why this is safe: nothing else in this project's demo code touches
// ROM after this point). Returns false (silent no-op, no music, not a
// crash) if LOCI isn't present, the file doesn't exist, or it exceeds
// ARKOS_MAX_MODULE_SIZE.
//
// Floppy target (-dSTORAGE_FLOPPY): loads via include/floppy.h's
// floppy_load(). This target has NO ROM/overlay concept at all -- see
// docs/floppy.md, "full RAM mapped at $C000-$FFFF for the whole session"
// -- so this is a plain load, no enable/disable step. Returns false if the
// file exceeds ARKOS_MAX_MODULE_SIZE.
#ifdef STORAGE_FLOPPY
bool arkos_load(uint8_t file_index);
#else
bool arkos_load(const char *path);
#endif

// Resets playback to the start of the currently loaded module (Linker
// position, per-channel Track/RegisterBlock state). Call once after a
// successful arkos_load(), before starting playback.
void arkos_init(void);

// One playback frame: advances the pattern/track state machines, decodes
// whichever RegisterBlock bytes are due this frame for each of the 3
// channels, and writes all 14 AY registers via include/ay.h's ay_write().
// Call at 50Hz via include/rasterirq.h's hrirq_add() -- MUST be
// __interrupt-qualified (see rasterirq.h's callback-safety note); this
// function already is.
__interrupt void arkos_tick(void);

// Silences all 3 channels (zero amplitude) without touching hrirq state --
// call hrirq_stop() separately if arkos_tick() should stop firing at all.
// Meant to precede a genuine restart (arkos_load() of a NEW module +
// arkos_init(), which always re-syncs the AY shadow from zero) -- NOT
// meant to precede arkos_pause()/arkos_resume()'s use case (see below):
// arkos_stop() writes the AY hardware directly without updating
// arkos_debug_shadow()'s own copy, so calling arkos_resume() after
// arkos_stop() would restore a stale pre-stop volume, not silence.
void arkos_stop(void);

// Silences all 3 channels LIKE arkos_stop(), but remembers each channel's
// current volume byte first and keeps the internal AY-register shadow
// consistent with the silence (unlike arkos_stop(), which leaves the
// shadow stale) -- see arkos_resume(). Does NOT touch playback position
// (Linker/Track/RegisterBlock pointers, per-channel rb_wait countdowns) at
// all: call hrirq_stop() separately to actually stop arkos_tick() from
// advancing that position while paused (matching arkos_stop()'s own
// convention of leaving hrirq state to the caller). Meant for a BRIEF,
// same-track pause (e.g. silencing music for the duration of a
// file_load()/floppy_load() call elsewhere in the demo -- see
// docs/arkos.md's "Pause vs. stop" section for why neither loading path
// is safe to call while arkos_tick() is ticking live), not for switching
// to a different module.
void arkos_pause(void);

// Restores the volumes arkos_pause() last snapshotted (instantly
// un-muting to the exact pre-pause loudness) and re-syncs the AY shadow
// to match. Playback position was never touched by arkos_pause(), so
// resuming arkos_tick() (via hrirq_start(), called separately) continues
// the SAME held note/pattern exactly where it left off -- not a restart.
// No-op if arkos_pause() was never called.
void arkos_resume(void);

// Returns a pointer to the last-computed AY registers 0-13 (14 bytes) --
// same testing-only rationale as pt3_debug_shadow() (Phosphoric can't read
// the AY chip's own internal state back). Not needed by normal playback
// code.
const uint8_t *arkos_debug_shadow(void);

// Returns true the first time this is called since the currently loaded
// module completed a full playthrough and looped back to its own start
// (the Linker position wrapping via its own loop-point pointer -- see
// arkos_advance_pattern()'s "end of song" branch in arkos.c) -- cleared
// back to false as soon as it's read, so poll this once per main-loop
// iteration rather than assuming it stays true. Intended use: switching
// to a different module once the current one finishes, so a long-running
// demo doesn't loop the exact same tune forever (see main.c's own
// music_check_toggle()). Always false before arkos_init() is ever called.
bool arkos_song_finished(void);

#pragma compile("arkos.c")

#endif // ARKOS_H
