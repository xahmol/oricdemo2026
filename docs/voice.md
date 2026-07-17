# AY-3-8912 voice-sample playback (voice.h)

Based on ChibiAkumas's Z80 tutorial series, [Lesson P35 — "Playing
Digital Sound with WAV on the AY!"](https://www.chibiakumas.com/z80/platform4.php#LessonP35)
— see "Attribution" below for exactly what was adapted.

Plays two hardcoded digitized voice clips, each once: "Welcome to Oric
Atmos" (from `src/section_logo.c`, once the Oric logo picture has loaded
but before the raster bars start animating) and "Thanks for watching"
(from `src/section_credits.c`, after the sunset picture loads but before
the credits scroller starts). Both use the same "digidrums"-style technique:
rapidly rewriting `AY_REG_VOL_A` (Channel A's amplitude register) from a
pre-quantized sample buffer turns the AY chip's own 4-bit/16-level
logarithmic volume steps into a crude software DAC, once that channel's
tone and noise generators are disabled. Expect **recognizable words, not
clear speech** — that resolution ceiling is inherent to the hardware
(16 amplitude steps total), independent of CPU budget or sample rate.
Include `voice.h`; it auto-compiles `voice.c` via `#pragma compile`.

```c
#ifdef STORAGE_FLOPPY
bool voice_load(uint8_t file_index, uint16_t size);
#else
bool voice_load(const char *path, uint16_t size);
#endif

void voice_play(uint16_t len, uint16_t timer1_period);
```

Same dual-signature convention as `arkos_load()`/`picture_load()`
(`docs/arkos.md`/`docs/picture.md`) — a real, intentional difference
between the two targets, not a bug. `voice_load()` loads a clip's own
`.bin` into a fixed address (`VOICE_SAMPLE_ADDR`, see below), capped at
`size`; `voice_play()` plays `len` bytes of whatever is currently
resident there, pacing playback via VIA Timer 1 reprogrammed to
`timer1_period` (1MHz cycles/sample, i.e. `1000000/rate`), blocking
synchronously for the clip's own duration. `size`/`len` are each
caller's own clip's exact converted byte count (known at compile time,
printed by `tools/oric_voiceconv.py` at conversion time) — same reason
`picture_load()`'s own `max_size` parameter exists. `timer1_period` MUST
match whatever rate the clip was actually converted at — the two clips
CAN use different rates (see "Per-clip sample rate" below), and the byte
stream itself carries no rate metadata to check this against, so getting
it wrong plays the clip at the wrong pitch/speed silently, not a crash.
**Callers must pass the PERIOD, not the rate**, computed as a
compile-time-constant literal (e.g. a `#define` dividing two other
`#define`s) — a real runtime `1000000UL / sample_rate` 32-bit division
inside `voice_play()` was tried and reverted after it overflowed the
main code/data/BSS budget by ~160 bytes (6502 has no hardware divide).

## Memory budget and address

The overlay-RAM window is `$C000-$F9FF` (14,848 bytes usable — see
`docs/arkos.md` for the full memory-layout rationale; `$FA00-$FFFF` is
permanently reserved for the floppy target's own resident loader on
**both** targets). It normally holds only the resident Arkos music
module. This project has two music tracks of different sizes:
`assets/steppingout.aky` (5,933 bytes) and `assets/boulesetbits.aky`
(7,117 bytes) — so **7,731 bytes** (`$FA00` minus the larger track's own
end address, `$DBCD`) is the largest a voice sample can be while staying
safe regardless of which track happens to be resident at the moment it
plays.

**Both clips load into the same fixed address**, one at a time — they're
never resident or playing simultaneously (welcome plays during section
#2 of 12; thanks plays during the very last section, #12), the same
reasoning Arkos itself uses to reuse `$C000`
for whichever music track is currently loaded. `VOICE_SAMPLE_ADDR` is
deliberately computed as `$FA00 - VOICE_SAMPLE_MAX_SIZE` (grows down from
the one boundary that's actually enforced elsewhere), not as a fixed
forward offset from `$C000`, and sized to the shared ceiling rather than
either clip's own smaller actual size. This means each clip's safety
doesn't depend on this project's own section ordering — every individual
clip's own real byte count MUST stay `<= 7731` for this guarantee to
hold; `tests/scripts/test_voiceconv.py` asserts this at build/test time
for BOTH clips, not just in this comment — re-run it before ever adding a
larger music track or a third clip.

Current clip sizes (well under the 7,731-byte ceiling, with room to
spare):
- `assets/voice_welcome.bin`: 6,080 bytes (1.52s at 4000Hz, after
  silence-trimming a 1.85s ElevenLabs TTS source clip).
- `assets/voice_thanks.bin`: 7,079 bytes (1.01s at 7000Hz, after
  silence-trimming a 1.44s ElevenLabs TTS source clip) — a HIGHER rate
  than welcome's own 4000Hz despite the shorter duration; see "Per-clip
  sample rate" below for why.

## Per-clip sample rate

Each clip is converted at whatever rate best fits its own content within
the shared 7,731-byte ceiling — `voice_play()`'s `timer1_period`
parameter (see above) exists specifically so the two call sites don't
have to share one compile-time rate. Each call site defines its own
`#define ..._PERIOD` (e.g. `VOICE_WELCOME_PERIOD 250U` for 4000Hz,
`VOICE_THANKS_PERIOD 142U` for 7000Hz) — a literal, not a computed
expression, so there's no ambiguity about whether it folds to a
compile-time constant.

`assets/voice_thanks.bin` uses 7000Hz, not `voice_welcome.bin`'s own
4000Hz, because "Thanks for watching" reads as noticeably less
recognizable than "Welcome to Oric Atmos" at the same 4000Hz — real
user feedback prompted the investigation. The likely cause: "Thanks for
watching" is more consonant/fricative-heavy ("th", "ks", "tch") than
"Welcome to Oric Atmos"'s own more vowel-heavy syllables, and those
higher-frequency sounds need more time resolution to stay distinguishable
at only 16 amplitude levels — a byte-histogram comparison of the two
clips' quantized levels (both fully using the 0-15 range, similarly
shaped distributions) ruled out a normalization/quantization difference
as the cause, pointing at sample rate (time resolution) instead. This
clip also happens to have by far the most headroom under the shared
ceiling (at 4000Hz it was only 4,045 bytes, barely half the budget,
vs. welcome's 6,080), so raising its rate to 7000Hz (7,079 bytes) costs
nothing against the ceiling. `voice_welcome.bin` was left at its
original 4000Hz/6,080-byte conversion — it was already confirmed
"audible and recognisable" on real hardware, and has much less headroom
to raise its own rate significantly (would top out around ~5100Hz
before exceeding the ceiling at its current duration) — no reason to
risk regressing something already working.

## Playback sequence

`voice_play(len, timer1_period)`, in order:

1. `hrirq_stop()` + `arkos_pause()` — same bracket `picture_load()`
   already uses for "pause music, block, resume" (see `docs/picture.md`).
   `hrirq_stop()` is required (not just `arkos_pause()`) because the
   playback loop needs the CPU and the AY chip fully to itself:
   `arkos_pause()` alone only silences the volume registers, it does not
   stop `arkos_tick()` (the 50Hz Arkos decode/IRQ callback) from firing.
2. **Snapshot AY register 7** (`AY_REG_MIXER`) via `arkos_debug_shadow()`
   (`include/arkos.h`) — see "Register 7 restoration" below for why this
   is provably correct, not just convenient.
3. Set register 7's Channel A tone+noise-disable bits (standard
   AY-3-8910/8912 mixer convention: 1=disabled), leaving every other bit
   — including bit 6, which `include/keyboard.c`'s `keyb_scan()` needs set
   for keypress sensing — exactly as snapshotted.
4. Reprogram VIA Timer 1's latch to `timer1_period` (same 3-write pattern
   `arkos_init()` uses to move Timer 1 off its ROM-default rate: latch
   low, latch high, then a `t1hi` write to force an immediate
   reload+start+IFR-clear).
5. Run the playback loop for `len` bytes: select `AY_REG_VOL_A` once,
   then for each sample byte, poll VIA Timer 1's IFR flag for the next
   underflow, acknowledge it (see "The IFR-acknowledge bug" below — this
   step is load-bearing, not optional), then write the byte via a
   write-data-only VIA/PCR sequence (standard digidrums optimization —
   the AY chip keeps a register selected across repeated write pulses
   until a new select-address cycle changes it, so there's no need to
   reselect `AY_REG_VOL_A` every sample).
6. Restore Timer 1's normal 50Hz latch (Arkos's own tick cadence) — all
   three writes again, not just the latch, or the in-flight counter
   (still counting from the last short reload) underflows once more at
   the old short period before the new latch takes effect.
7. Restore register 7 to **exactly** its step-2 snapshot.
8. `arkos_resume()` + `hrirq_start()`.

## The IFR-acknowledge bug (found via real hardware testing)

The first shipped version of this playback loop read `VIA.t1lo` to
acknowledge/clear Timer 1's IFR flag using the same C-level idiom
`rasterirq.c`'s own `_hrirq_handler` uses successfully:
`volatile uint8_t ack = VIA.t1lo; (void)ack;`. On real hardware this
produced audible garbage — a burst of noise far shorter than the clip's
real duration, not recognizable speech.

Root cause, found by reading the actual compiled assembly output
(`build/oricdemo.asm`): Oscar64's optimizer **silently eliminated the
read entirely** in this ordinary (non-`__hwinterrupt`) function context.
`VIA` is a plain, non-`volatile`-qualified pointer cast
(`#define VIA (*((VIA_t *)0x0300))`, `include/oric.h`), and outside
`__hwinterrupt` context, a local `volatile` variable that's immediately
discarded isn't enough to force the underlying hardware read to survive
— confirmed by comparing the compiled output of this exact idiom in both
contexts: it survives inside `_hrirq_handler` (a genuine `LDA $0304`
instruction is present) but was absent entirely from `voice_play_loop()`.

Without that read, the IFR flag is never actually acknowledged, so it
stays set after the very first Timer 1 underflow — every sample after the
first then writes back-to-back with no pacing at all (limited only by the
loop's own few-cycle overhead, not the intended per-sample period),
compressing the whole clip into a fraction of a second. This exactly
matched the real-hardware symptom.

**Fix**: `__asm volatile { lda $0304 }` — genuine inline assembly,
Oscar64's own documented "prevent optimization" mechanism (see
`oscar64manual.md`'s Inline Assembly section), immune to this class of
elimination. Confirmed working on real hardware after this fix
("Audible and recognisable").

## Register 7 restoration

`arkos_ay_shadow[]` (Arkos's private dirty-tracking cache,
`include/arkos.c`) is not updated by any writes made outside `arkos.c`
itself. `arkos_tick()` writes register 7 every tick, but
`arkos_ay_write_if_changed()` **skips the real hardware write whenever
the computed target equals the shadow's cached value** — a real,
previously-hit bug class in this exact project (`docs/arkos.md`'s "Third
real bug": keyboard sensing breaks if register 7 bit 6 ends up wrong).
If `voice_play()` left register 7 in a state the shadow doesn't know
about, and the very next `arkos_tick()` after resume happened to
recompute the same value the shadow already holds (likely, since
playback position is frozen during the pause — nothing about the track
data changed), the corrective rewrite would be silently skipped, leaving
keyboard sensing broken.

The fix is provably correct, not just probably fine: `arkos_debug_shadow()`
(`include/arkos.h`, despite its "debug"-sounding name, a legitimate public
accessor already used elsewhere in this project) exposes the shadow's own
current value. Since `hrirq_stop()` (step 1 above) guarantees
`arkos_tick()` — the shadow's only writer anywhere in the runtime, and
register 7's only other writer at all — cannot fire again until
`hrirq_start()` much later, and `arkos_pause()` doesn't touch register 7
either, the snapshot taken in step 2 is guaranteed to still match real
hardware right up until `voice_play()` itself changes it in step 3. Restoring
that exact byte in step 7 puts hardware back in perfect sync with the
(unmodified) shadow — no new AY read protocol needed.

## Why welcome plays from section_logo.c, not section_splash.c

Real-hardware testing reported a few seconds of black screen after the
idi8b splash dissolves out, before the Oric logo picture appears — this
was investigated as a candidate fix for that gap: play "Welcome to Oric
Atmos" from `section_splash.c`'s own `SPLASH_HOLD` state instead of from
`section_logo_init()`, so the sample's load+playback time (pure audio,
never touching `HIRESVRAM`) overlaps with the splash's still-visible
hold phase instead of running after the transition wipe has already
cleared the screen.

This was tried, then reverted, once the actual cause of the black-screen
gap was pinned down: it's `section_logo_init()`'s own `picture_load()`
call — the Oric logo picture's real I/O load latency from LOCI/floppy
hardware — not the voice sample. `picture_load()` runs regardless of
where the voice sample plays; relocating `voice_play()` to the splash
section only removes the *sample's own* ~1.5-2s from stacking on top of
that picture-load gap, it does nothing about the gap itself. Since
avoiding the picture-load delay specifically was the actual goal (not
just shaving the sample's own time off the total), and the picture
itself can't be preloaded (see below), moving the sample doesn't serve
that goal and was reverted: "Welcome to Oric Atmos" plays from
`section_logo_init()`, right after `picture_load()`, exactly as it did
originally. `section_splash.c`'s `SPLASH_HOLD_TICKS` is back to its
original 60-tick (~4.4s) fixed hold, with no voice call in that phase.

**The logo picture itself cannot be preloaded while the splash is still
on screen** — this remains the real constraint behind the residual gap.
`picture_load()` writes directly into `HIRESVRAM` (`docs/picture.md`'s
own documented design — no off-screen staging buffer), so there is no
way to load the next section's picture while the splash is still the
thing actually on screen without a genuine ~8000-byte off-screen buffer,
which doesn't fit in either the main code/data/BSS budget (already
tight — this exact feature's own earlier iteration hit a 10-byte
overflow adding one small 84-byte buffer) or the overlay-RAM window (at
most 7,731 bytes free regardless of which music track is resident,
smaller than the 8,000 bytes a picture buffer needs). The logo picture's
own load latency remains a real, unresolved gap — not something this
voice feature (in either call-site arrangement) fixes.

## Conversion tool

`tools/oric_voiceconv.py` (stdlib Python only — no numpy, no `audioop`,
which is deprecated/removed as of Python 3.13) takes a mono WAV file,
trims leading/trailing silence, resamples via linear interpolation to a
target rate (default 4000Hz), peak-normalizes, and linearly quantizes
each sample to 0-15 (a raw byte stream, one byte per sample). A
`--max-bytes` flag (default 7731, matching the shared ceiling above)
makes the tool fail loudly rather than silently truncate if a source clip
would produce an oversized sample.

Converting from another source format (e.g. an MP3 TTS export) is a
one-off prep step outside this tool:
```
ffmpeg -i in.mp3 -ar 22050 -ac 1 out.wav
python3 tools/oric_voiceconv.py out.wav assets/voice_welcome.bin --rate 4000
python3 tools/oric_voiceconv.py thanks.wav assets/voice_thanks.bin --rate 7000
```
Same convention as `oric_pictconv.py` taking an already-sourced image
rather than fetching one itself. `--rate` is chosen per clip (see
"Per-clip sample rate" above) — always re-check the tool's own printed
byte count against the shared 7,731-byte ceiling, and keep the matching
`voice_play()` call site's `..._PERIOD` `#define` (`1000000/rate`, e.g.
250 for 4000Hz, 142 for 7000Hz) in sync with whatever `--rate` was
actually used.

## Attribution

The playback **technique** itself (quantize a WAV file down to a low bit
depth, mute the channel's tone/noise generators so only its volume
register remains, repeatedly rewrite that register at a paced rate to
reconstruct the waveform, silence it when done) is based on
ChibiAkumas's Z80 tutorial series, [Lesson P35 — "Playing Digital Sound
with WAV on the AY!"](https://www.chibiakumas.com/z80/platform4.php#LessonP35)
— the same general approach that lesson teaches for Z80-based AY-3-8910
systems (Amstrad CPC, MSX, etc.), including its own "ChibiWave
Converter" tool (WAV → packed 1/2/4-bit-per-sample data) that
`tools/oric_voiceconv.py` parallels for this project's own pipeline.
What was adapted, not copied: this project is bare-metal C via Oscar64
targeting the Oric's 6502 + AY-3-8912 (not Z80 assembly), paces playback
via the Oric's own VIA Timer 1 IFR polling (see "Playback sequence"
above) rather than a Z80 delay-loop counter, quantizes to the AY chip's
full 4-bit/16-level range rather than the lesson's 1/2/4-bit options,
and adds this project's own register-7 shadow-restore mechanism (see
"Register 7 restoration" above) — a real bug class specific to this
codebase's own Arkos player, not something the original lesson needed to
handle.

Both `assets/voice_welcome.bin` and `assets/voice_thanks.bin` source
clips are text-to-speech renders via ElevenLabs ("Pepper" voice) — see
`README.md`'s own Credits section.
