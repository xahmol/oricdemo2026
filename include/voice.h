// voice.h - AY-3-8912 "digidrums"-style voice-sample playback for Oric Atmos
//
// Based on ChibiAkumas's Z80 tutorial series, Lesson P35 "Playing Digital
// Sound with WAV on the AY!" (chibiakumas.com/z80/platform4.php#LessonP35)
// -- same general technique (quantize a WAV file down to a low bit depth,
// mute the channel's tone/noise so only its volume register remains,
// repeatedly rewrite that register at a paced rate to reconstruct the
// waveform, silence it when done). Adapted: paced via the Oric's own VIA
// Timer 1 IFR polling (see voice.c) instead of a Z80 delay-loop counter,
// written in C via Oscar64 instead of Z80 assembly, and quantized to the
// AY-3-8912's full 4-bit range (that lesson's own tooling supports
// 1/2/4-bit) via tools/oric_voiceconv.py instead of its ChibiWave
// Converter.
//
// Two hardcoded voice clips -- "Welcome to Oric Atmos" (played once from
// section_logo.c, right after the logo picture loads) and "Thanks for
// watching" (played once from section_credits.c) -- played back by
// rapidly rewriting AY_REG_VOL_A (Channel A's amplitude register) from a
// pre-quantized sample buffer. The AY chip's own 4-bit/16-level
// logarithmic volume steps become a crude software DAC once that
// channel's tone/noise generators are disabled. Expect "recognizable
// words", not clear speech -- that resolution ceiling is inherent to the
// hardware, independent of CPU budget or sample rate. See
// tools/oric_voiceconv.py for the offline WAV->sample conversion and
// docs/voice.md for the full design writeup (memory budget, register-7
// restoration, and why this doesn't depend on which music track is
// resident).
//
// MEMORY: both clips load into the SAME fixed address, one at a time --
// they're never resident or playing simultaneously (welcome plays near
// the very start of the demo, thanks plays during the very last section),
// the same reasoning Arkos itself uses to reuse $C000 for whichever music
// track is currently loaded. That address is computed as $FA00 minus
// VOICE_SAMPLE_MAX_SIZE -- grows DOWN from $FA00 (the one overlay-RAM
// boundary genuinely enforced elsewhere, see arkos.h's own
// ARKOS_MAX_MODULE_SIZE comment), not up from either Arkos music track's
// own current size. This is deliberate: it keeps every voice sample safe
// regardless of which of the two music tracks (assets/steppingout.aky,
// 5933 bytes, or assets/boulesetbits.aky, 7117 bytes) happens to be
// resident at $C000 when it plays, rather than depending on this
// project's own section ordering. Every individual clip's own real byte
// count MUST stay <= VOICE_SAMPLE_MAX_SIZE (7731 bytes, $FA00 - $DBCD,
// i.e. $FA00 minus boulesetbits.aky's own end address) for this guarantee
// to hold -- see tests/scripts/test_voiceconv.py's own build-time
// assertion of this exact ceiling (checked against BOTH clips), and
// re-check it before ever adding a larger music track or a third clip.
//
// PLAYBACK: fully blocking, interrupts disabled for the duration (matches
// this project's dominant "interrupts off" convention everywhere except
// rasterirq.c itself) -- pauses Arkos (hrirq_stop()+arkos_pause(), same
// bracket include/picture.c's picture_load() already established for
// "pause music, block, resume"), reprograms VIA Timer 1's latch to the
// sample rate for the duration (same technique arkos_init() uses to move
// Timer 1 off its ROM-default rate), then restores everything (Timer 1's
// normal 50Hz Arkos cadence, AY register 7's exact pre-playback value, via
// arkos_debug_shadow() -- see voice.c's own header comment for why that's
// provably correct and not just "probably fine") before resuming.

#ifndef VOICE_H
#define VOICE_H

#include <stdint.h>
#include <stdbool.h>

// Shared ceiling every individual clip's own real byte count must stay
// under -- see this file's own MEMORY comment above. VOICE_SAMPLE_ADDR is
// sized to this shared ceiling (not either clip's own smaller actual
// size), so both clips load at the exact same address.
#define VOICE_SAMPLE_MAX_SIZE   7731U
#define VOICE_SAMPLE_ADDR       (0xFA00U - VOICE_SAMPLE_MAX_SIZE)

// Loads a voice-sample .bin into VOICE_SAMPLE_ADDR, capped at `size` --
// same dual-signature convention as arkos_load()/picture_load() -- see
// arkos.h's own comment for why this isn't a bug:
//   - Tape/LOCI target: voice_load(const char *path, uint16_t size), via
//     include/loci.h's file_load().
//   - Floppy target (-dSTORAGE_FLOPPY): voice_load(uint8_t file_index,
//     uint16_t size), via include/floppy.h's floppy_load().
// `size` is each caller's own clip's exact converted byte count (known
// at compile time, printed by tools/oric_voiceconv.py at conversion
// time) -- same "known ahead of time, not read from a length prefix"
// reason picture_load()'s own max_size parameter exists.
#ifdef STORAGE_FLOPPY
bool voice_load(uint8_t file_index, uint16_t size);
#else
bool voice_load(const char *path, uint16_t size);
#endif

// Plays `len` bytes of the sample already resident at VOICE_SAMPLE_ADDR
// (loaded via voice_load() with the same size), pacing playback via
// VIA Timer 1 reprogrammed to `timer1_period` (1MHz cycles/sample,
// i.e. 1000000/rate) -- MUST match the rate the clip was actually
// converted at (tools/oric_voiceconv.py's own --rate), which can differ
// per clip (there's no rate metadata in the byte stream itself --
// getting this wrong plays the clip at the wrong pitch/speed, not a
// crash). Callers pass the PERIOD, not the rate, and MUST compute it as
// a compile-time-constant literal (e.g. a #define dividing two other
// #defines) -- a real 32-bit runtime division here (1000000UL /
// sample_rate) previously overflowed the main code/data/BSS budget by
// ~160 bytes (6502 has no hardware divide). Blocks synchronously until
// playback finishes -- see this file's own PLAYBACK comment above for
// the full pause/mute/restore/resume sequence this performs.
void voice_play(uint16_t len, uint16_t timer1_period);

#pragma compile("voice.c")

#endif // VOICE_H
