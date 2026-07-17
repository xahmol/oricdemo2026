// voice.c - see voice.h.

#include "voice.h"
#include "oric.h"
#include "ay.h"
#include "arkos.h"
#include "rasterirq.h"
#ifdef STORAGE_FLOPPY
#include "floppy.h"
#else
#include "loci.h"
#include "homedir.h"
#endif

#ifdef STORAGE_FLOPPY
bool voice_load(uint8_t file_index, uint16_t size)
{
    int16_t r;
    hrirq_stop();
    arkos_pause();
    r = floppy_load(file_index, (void *)VOICE_SAMPLE_ADDR, size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#else
// Real-hardware fix: see homedir.h's own header comment (same fix, same
// reason, as arkos.c's arkos_load()/picture.c's picture_load()). Reuses
// arkos_load()'s own scratch buffer (arkos_load_path_buf(), see arkos.h's
// own comment on that declaration) instead of allocating a third private
// HOMEDIR_MAXLEN-sized static buffer here -- the main code/data/BSS
// budget (~36.1KB, docs/hires.md) is tight enough that a third copy of
// that ~96-byte buffer didn't fit. Safe: none of arkos_load()/
// picture_load()/two voice_load() calls (welcome, thanks) ever run
// concurrently with each other, and each freshly repopulates the buffer
// via homedir_join() before using it.
bool voice_load(const char *path, uint16_t size)
{
    int16_t r;
    char *path_buf = arkos_load_path_buf();
    hrirq_stop();
    arkos_pause();
    homedir_join(path_buf, path);
    r = file_load(path_buf, (void *)VOICE_SAMPLE_ADDR, size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#endif

// -------------------------------------------------------------------------
// Playback
// -------------------------------------------------------------------------

// voice_play()'s own `timer1_period` argument (1MHz cycles/sample) must
// match whatever rate the clip was actually converted at
// (tools/oric_voiceconv.py's own --rate: period = 1000000/rate) -- each
// clip can use a DIFFERENT rate (the two clips' own byte streams carry
// no rate metadata, so getting this wrong plays the clip at the wrong
// pitch/speed, not a crash). Callers pass the PERIOD, not the rate
// itself, specifically so this is always a compile-time-constant literal
// -- a real, previously-hit BSS overflow confirmed a runtime 32-bit
// `1000000UL / sample_rate` division here costs ~160 bytes of code
// (6502 has no hardware divide), which the tight main code/data/BSS
// budget (~36.1KB, docs/hires.md) cannot absorb.

// AY register 7 (mixer) bits to disable Channel A's tone and noise
// generators (standard AY-3-8910/8912 convention: 1=disabled) --
// independently confirmed against arkos_tick()'s own bit-assembly logic
// (include/arkos.c), not just datasheet folklore. Channel A specifically:
// bit0=tone A, bit3=noise A.
#define VOICE_MIXER_MUTE_A   0x09U

// Same PCR (VIA.pcr = $030C) values as include/ay.c's own SND_SELSETADDR/
// SND_SELWRITE/SND_DESELECT -- not exposed via ay.h (ay.c keeps them
// file-local), so redefined here rather than modifying that file's public
// surface for one caller. See ay.c's own header comment for the full
// BC1/BDIR protocol writeup this implements.
#define VOICE_SND_SELWRITE    0xFDU
#define VOICE_SND_SELSETADDR  0xFFU
#define VOICE_SND_DESELECT    0xDDU

// The actual per-sample write loop. Selects AY_REG_VOL_A ONCE, then only
// repeats the write-data/deselect PCR phase per sample (standard digidrums
// technique -- the AY chip keeps a register selected across repeated
// write pulses until a new select-address cycle changes it) -- cheaper
// than a full ay_write() call per sample, which always redoes the select
// phase too. No PHP/SEI/PLP bracket here (unlike ay_write()): voice_play()
// already holds interrupts off for this whole routine via hrirq_stop(),
// so redoing that bracket per sample would only cost cycles this tight a
// loop can't spare.
//
// __noinline: keeps this loop's compiled shape independent of its one
// call site and its own live-local footprint minimal -- cheap defense-in-
// depth against a real, previously-confirmed Oscar64 -O2 whole-program
// inliner bug (see src/section_logo.c's set_rows()/paint_bar() header
// comments for the full mechanism this project already hit twice). This
// loop's arguments are genuinely runtime-varying at its one call site, a
// different shape from that bug's actual trigger (compile-time-constant
// arguments) -- but the annotation costs nothing measurable, and this
// project's own established posture is to apply it regardless.
__noinline static void voice_play_loop(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    VIA.pra2 = AY_REG_VOL_A;
    VIA.pcr  = (uint8_t)VOICE_SND_SELSETADDR;
    VIA.pcr  = (uint8_t)VOICE_SND_DESELECT;

    for (i = 0; i < len; i++)
    {
        // Wait for Timer 1 to underflow (IFR bit 6) -- polled, not IRQ-
        // driven (interrupts are off for this whole routine). IFR is a
        // hardware flag independent of the CPU's interrupt-enable state,
        // so this keeps ticking over correctly even under sei -- the same
        // fact _hrirq_handler's own ack mechanism relies on.
        while ((VIA.ifr & 0x40) == 0)
            ;

        // Reading t1lo ($0304) clears the IFR flag (standard 6522
        // semantics, matching rasterirq.c's own identical-purpose read).
        // MUST be genuine __asm volatile, not a C-level
        // "volatile uint8_t ack = VIA.t1lo;" -- confirmed the hard way:
        // that exact idiom (copied from rasterirq.c's own _hrirq_handler,
        // where it DOES survive) was silently eliminated here by Oscar64's
        // optimizer -- VIA itself is a plain, non-volatile-qualified
        // pointer cast (see oric.h's own "#define VIA
        // (*((VIA_t*)0x0300))"), and outside __hwinterrupt context a
        // local volatile variable that's immediately discarded isn't
        // enough to force the READ itself to survive. Without this read,
        // the IFR flag is never actually acknowledged, so it stays set
        // after the very first underflow -- every sample after the first
        // then writes back-to-back with no pacing at all (limited only by
        // this loop's own few-cycle overhead, not the intended per-sample
        // period), compressing the whole clip into a fraction of a
        // second. Confirmed via a real playback test: exactly this
        // symptom ("too fast", "not recognizable as voice"). __asm
        // volatile (Oscar64's own documented "prevent optimization"
        // mechanism -- see oscar64manual.md's Inline Assembly section) is
        // immune to this class of elimination.
        __asm volatile { lda $0304 }

        VIA.pra2 = data[i];
        VIA.pcr  = (uint8_t)VOICE_SND_SELWRITE;
        VIA.pcr  = (uint8_t)VOICE_SND_DESELECT;
    }
}

void voice_play(uint16_t len, uint16_t timer1_period)
{
    uint8_t saved_r7;

    hrirq_stop();
    arkos_pause();

    // Snapshot the real, current AY register 7 value via Arkos's own
    // shadow cache -- provably accurate here, not just probably fine:
    // hrirq_stop() above guarantees arkos_tick() (the shadow's only
    // writer anywhere in this program, and register 7's only writer
    // besides this function) cannot fire again until hrirq_start() much
    // later, and arkos_pause() doesn't touch register 7 either. See
    // voice.h's own header comment for the real bug class this avoids --
    // arkos_ay_write_if_changed()'s shadow-equals-target skip optimism
    // silently leaving keyboard sensing broken if this were left wrong.
    saved_r7 = arkos_debug_shadow()[AY_REG_MIXER];
    ay_write(AY_REG_MIXER, (uint8_t)(saved_r7 | VOICE_MIXER_MUTE_A));

    // Reprogram Timer 1's latch to the sample period -- same exact
    // 3-write pattern arkos_init() uses to move Timer 1 off its ROM-
    // default rate (include/arkos.c): latch low/high, then a T1C-H write
    // to force an immediate reload+start+IFR-clear.
    VIA.t1llo = (uint8_t)(timer1_period & 0xFF);
    VIA.t1lhi = (uint8_t)(timer1_period >> 8);
    VIA.t1hi  = (uint8_t)(timer1_period >> 8);

    voice_play_loop((const uint8_t *)VOICE_SAMPLE_ADDR, len);

    // Restore Timer 1's normal 50Hz rate (Arkos's own tick cadence) --
    // same pattern, same TIMER1_50HZ constant arkos_init() itself uses.
    // All three writes, not just the latch -- otherwise the in-flight
    // counter (still counting from the last short reload) underflows
    // once more at the OLD short period before the new latch takes
    // effect on the following reload.
    VIA.t1llo = (uint8_t)(TIMER1_50HZ & 0xFF);
    VIA.t1lhi = (uint8_t)(TIMER1_50HZ >> 8);
    VIA.t1hi  = (uint8_t)(TIMER1_50HZ >> 8);

    // Restore register 7 to EXACTLY its pre-playback value -- see this
    // function's own snapshot comment above.
    ay_write(AY_REG_MIXER, saved_r7);

    arkos_resume();
    hrirq_start();
}
