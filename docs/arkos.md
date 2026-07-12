# Arkos Tracker music player (arkos.h)

Plays Arkos Tracker 2's `.aky` export format on the AY-3-8912, ticking via
[rasterirq.h](rasterirq.md) and loading module data at runtime from
[LOCI](loci.md) or the [floppy target](floppy.md), depending on build
target. Include `arkos.h`; it auto-compiles `arkos.c` via `#pragma
compile`.

## Attribution

Original AKY music player - V1.0, by Julien Nevo a.k.a. Targhan/Arkos, with
CPC PSG-sending optimizations by Madram/Overlanders, December 2016. 6502
conversion (Apple II + Mockingboard | Oric 1/Atmos) by Arnaud Cocquiere
a.k.a GROUiK/FRENCH TOUCH. OSDK/Oric sample-program adaptation by Mickael
Pointier (Dbug):
[github.com/Oric-Software-Development-Kit/Arkos-Music-Player](https://github.com/Oric-Software-Development-Kit/Arkos-Music-Player)
(`akyplayer.s`).

**This module is a from-scratch C rewrite, not a transliteration of the
6502 assembly's own instruction sequences** -- but its *logic* (the
Linker/Track/RegisterBlock decode, including every bit-rotation branch) was
traced directly and precisely from `akyplayer.s`, instruction by
instruction, and validated with a from-scratch Python decode replica run
against real `.aky` file bytes before any C was written -- the same
methodology this project's PT3 player investigation eventually settled on
after several rounds of hand-tracing mistakes. See "What's precisely
traced" below.

**Replaces this project's earlier PT3 (Vortex Tracker) player**, archived
on the `pt3` branch after several rounds of confirmed decode-bug fixes
still didn't produce music the user found satisfying, and its runtime
overhead was judged too high (see that branch's own `ARCHIVE_NOTE.md`).

## Why this format needs a fixed load address (no relocation)

Unlike PT3 (which loads into an ordinary linker-placed static buffer and
uses module-relative offsets throughout), Arkos Tracker's own AKY exporter
bakes **absolute 16-bit pointers** into the file at export time -- the
"Encode to address" field in Arkos Tracker's own export dialog. There is
no header field recording what address was used; it's purely an export-time
convention that has to be known and matched exactly, or every pointer in
the file is simply wrong.

**This project's own convention: every `.aky` file must be exported to
`$C000`** (`ARKOS_MODULE` in `arkos.h`), matching this project's own
overlay-RAM/floppy-RAM music buffer -- see "Memory layout" below for why
that specific address. A relocation-pass design (compute a fixed delta and
patch every pointer field at load time) was considered and worked out in
detail before this decision was made -- see the project's own planning
history for the two-pass dedup-and-patch algorithm that would have been
needed (Linker entries' track pointers need dedup-by-original-value before
patching, since tracks are genuinely shared/reused across patterns;
RegisterBlocks themselves never need touching, since they contain no
embedded pointers at all) -- but a fixed load address matching the file's
own export address is simpler and carries less risk, so that's what's
actually implemented.

## Memory layout: why `$C000`, and why not the full 16KB

Both this project's HIRES runtimes (`include/oric_crt_hires.c`,
`include/oric_crt_floppy_hires.c`) leave `$C000-$FFFF` alone entirely (see
each file's own memory-layout comment) rather than carving the music
buffer out of the ~36KB main code/data/BSS budget:

- **Tape/LOCI target**: real Atmos ROM normally occupies `$C000-$FFFF`.
  `oric.h`'s `OVERLAY_ON`/`OVERLAY_OFF` (via `loci.c`'s
  `enable_overlay_ram()`/`disable_overlay_ram()`, writing `MICRODISCCFG` at
  `$0314`) bank in 16KB of genuine, directly-addressable RAM there instead
  -- confirmed working via a real emulator test (Phosphoric): write a
  marker byte, read it back correctly while overlay RAM is enabled, then
  confirm the same address shows *different* content once overlay RAM is
  disabled again (proving real bank-switching, not just "it happened to be
  writable"). `arkos_load()` enables overlay RAM once, before loading, and
  leaves it enabled for the rest of the program's runtime -- this project's
  own demo code never calls ROM after that point, so there's nothing to
  lose. **Known gap**: this specific mechanism is empirically confirmed
  only for Phosphoric; a plain Oricutron tape boot (`make run`, no disk
  controller attached) very likely does NOT provide working overlay RAM,
  since nothing in that configuration responds to `$0314` writes at all
  (Oricutron's own Microdisc emulation, which does correctly implement
  ROM-disable-on-`$0314`, is only present when a disk controller is
  actually attached -- e.g. via `--disk-rom`). This is an accepted,
  deliberate scope decision, not an oversight -- `make run-phos` and the
  floppy target both work.
- **Floppy target**: no ROM/overlay concept at all -- see
  [floppy.md](floppy.md): "full RAM mapped at `$C000-$FFFF` for the whole
  session." `arkos_load()` is a plain `floppy_load()` call, no
  enable/disable step needed.

**`ARKOS_MAX_MODULE_SIZE` is 14848 bytes (`$C000-$F9FF`), not the full
16384.** `$FA00-$FFFF` is permanently reserved for the floppy target's own
resident loader (`tools/floppy/loader.c`) and its fixed API cells
(`$FFEF-$FFFF`) -- overwriting them would corrupt the loader that's
supposed to still be usable for the rest of the program's runtime. The
tape/LOCI target doesn't strictly need this reservation (nothing of this
project's lives at `$FA00-$FFFF` there), but both targets share the one
constant/convention rather than maintaining two different maximum sizes.

This size constraint is also why the demo's music file went through a few
iterations: the originally-picked `AsianAgent.aky` (15895 bytes) doesn't
fit; `Morons.aky` (12399 bytes) does, and was re-exported to `$C000` for
this project's own use (the copy in the upstream reference repo is still
baked for `$7000`, that project's own convention -- not directly usable
here without re-exporting). The song actually shipped is
`assets/steppingout.aky` ("Mr.Lou - Dewfall Productions - Stepping out
(2019)", 5933 bytes), a real, properly re-exported file supplied directly
rather than one of the reference repo's own bundled songs.

### Real bug found and fixed: overlay RAM silently breaks the real 6502 IRQ vector

Enabling overlay RAM on the tape/LOCI target banks OUT real Atmos ROM for
the *entire* `$C000-$FFFF` window -- including the hardware IRQ vector at
`$FFFE`/`$FFFF`. ROM's own interrupt handler there is what normally chains
down into `rasterirq.c`'s low-RAM software vector (`$0245`/`$0246`); with
ROM gone, nothing provides that first hop at all. The first real Timer 1
IRQ after `hrirq_start()` then vectors into whatever garbage happens to be
sitting in the freshly-banked-in RAM -- confirmed as a real, reproducible
crash via a Phosphoric capture: the real demo ran fine for a few seconds,
then the CPU's own program counter started oscillating between `$FFFF` and
`$0002` every frame, and a RAM-dump screenshot showed the machine had
silently reset back to the ROM's own BASIC prompt. Bisecting by cycle count
pinned the crash to within a couple of frames of `hrirq_start()` being
called -- i.e. the very first real interrupt, not a slow leak.

`tools/floppy/loader.c` already solved this *exact* problem on the floppy
target (which also has no ROM at `$C000-$FFFF`, for a different reason --
see [floppy.md](floppy.md)'s "IRQ-vector bridge" section): plant a tiny
3-byte `JMP ($0245)` stub in RAM and point `$FFFE`/`$FFFF` at it, so the
hardware vector always resolves to an indirect jump through the same
low-RAM cell `rasterirq.c` itself manages -- valid regardless of whether
`hrirq_init()` has run yet. `arkos_load()`'s tape/LOCI variant now does the
same thing (`arkos_setup_irq_bridge()` in `arkos.c`), once, right after a
successful load -- except the 3-byte stub lives in ordinary (non-overlay)
RAM here, not floppy-loader-resident code, so it stays valid regardless of
overlay-RAM banking state. Verified fixed via the same bisection: the real
demo now runs stably for 60M+ cycles (~78 simulated seconds) on the tape/
LOCI target and 30M+ cycles on the floppy target, with the bird/background
still rendering correctly in both cases.

### Real bugs found and fixed: two separate Oscar64 `-O2` miscompilations

Both found via the same technique -- Phosphoric RAM-dump instrumentation
comparing a value computed in isolation (a tiny standalone helper function
given the same inputs) against the same computation inline in its real
call site, since a genuine compiler bug was suspected once results
differed between two builds that should have been logically identical.

1. **A `while` loop in `arkos_init()`'s header-parsing code silently never
   executed its body**, even though the loop-controlling variable
   (`chan_count`) was confirmed correct (3) via a direct raw-memory peek at
   the exact same address, at the exact same point in execution. This left
   `arkos_linker_ptr` 4 bytes short of the real Linker start, corrupting
   every downstream Track/RegisterBlock read. Fixed by eliminating the loop
   entirely: this player only ever supports a single PSG group (3
   channels, the Oric's only AY chip -- see the API section below), so the
   header is always exactly 6 bytes and no loop was ever actually needed.
2. **`arkos_peek16((uint16_t)(ch->track_ptr + 1))`, correct when called in
   isolation, computed the wrong address when inlined directly into
   `arkos_tick()`'s own large local-variable set** (a for-loop over 3
   channels, several `uint8_t` locals, and a large multi-field `ArkosRB`
   struct all live at once). Fixed by extracting the Track-triple-read
   logic and the AY-register-application logic into their own small static
   helper functions (`arkos_channel_start_triple()`,
   `arkos_apply_registerblock()`), each getting an independent, much
   smaller stack frame -- `arkos_tick()` itself now has far fewer
   simultaneously-live locals.

Both bugs are consistent with a single underlying cause: Oscar64's
register/zero-page-slot allocator making a mistake once too many locals are
simultaneously live in one function -- the same general territory as the
"function too complex for interrupt" error the RegisterBlock dispatch trees
hit earlier (see `arkos_rb_initial()`'s own comment). Reducing a function's
local-variable footprint, not just its raw line count, is the practical
lesson for any future change to `arkos.c`'s hot path.

## API

```c
#ifdef STORAGE_FLOPPY
bool arkos_load(uint8_t file_index);
#else
bool arkos_load(const char *path);
#endif
void arkos_init(void);
__interrupt void arkos_tick(void);
void arkos_stop(void);
void arkos_pause(void);
void arkos_resume(void);
const uint8_t *arkos_debug_shadow(void);
```

`arkos_load()` loads a `.aky` module directly into the fixed `$C000`
buffer (`ARKOS_MODULE`) -- see "Memory layout" above for the two different
target-specific behaviors. Returns `false` (silent no-op, no music, not a
crash) on any failure (no LOCI/floppy device, file not found, file too
large for `ARKOS_MAX_MODULE_SIZE`).

`arkos_init()` resets playback to the start of the Linker (order
position) and all per-channel Track/RegisterBlock state. Call once after a
successful `arkos_load()`, before starting playback.

`arkos_tick()` is one playback frame: advances the pattern/track state
machines, decodes whichever RegisterBlock bytes are due this frame for
each of the 3 channels, and writes all 14 AY registers. Already
`__interrupt`-qualified -- wire it up via `hrirq_add()` exactly like
`pt3_tick()` was:

```c
arkos_load("music.aky");
arkos_init();
hrirq_init();
hrirq_add(100, arkos_tick);
hrirq_start();
```

`arkos_stop()` silences all 3 channels without touching `hrirq` state --
call `hrirq_stop()` separately to stop `arkos_tick()` firing at all.

`arkos_debug_shadow()` returns the last-computed 14 AY register values --
testing only, same rationale as `pt3_debug_shadow()` (Phosphoric can't
read the AY chip's own internal state back).

### Pause vs. stop

Neither `file_load()` (tape/LOCI) nor `floppy_load()` (floppy target) is
safe to call while `arkos_tick()` is ticking live via an active
`hrirq_start()`: the LOCI path would be progressively overwriting the
exact `$C000` `ARKOS_MODULE` buffer `arkos_tick()` is concurrently
decoding (a genuine data race, nothing pauses playback during a load),
and the floppy target's resident loader has a tight WD1793 DRQ-polling
timing budget with no interrupt protection at all -- a raster IRQ firing
mid-sector-read could plausibly corrupt it. Any code that needs to load a
file while music might already be playing (switching to a different
track, or loading an unrelated asset like a picture mid-demo) MUST
`hrirq_stop()` first and `hrirq_start()` again after -- but what happens
to the *music* itself during that window differs depending on intent:

- **Switching to a genuinely different track** (`arkos_stop()` then
  `arkos_load()` of a NEW module, then `arkos_init()`): `arkos_stop()`'s
  own staleness (see its own doc comment -- it writes the AY hardware
  directly without updating the shadow) doesn't matter here, because
  `arkos_init()` always resets the shadow to all-zero anyway as part of
  starting the new module fresh (`arkos_pattern_counter = 1` forces an
  immediate pattern-load on the new track's very first tick). This is a
  restart, by design.
- **A brief pause with the SAME track resuming afterward** (e.g.
  silencing music for the duration of an unrelated `file_load()`/
  `floppy_load()` call elsewhere in the demo, not a track switch):
  `arkos_stop()` + a later `hrirq_start()` with no `arkos_init()` in
  between would leave a real bug. `arkos_apply_registerblock()` only
  rewrites a channel's volume register when the current tick's
  RegisterBlock frame explicitly says to (`rb->wrote_vol`) -- a sustained
  note routinely spans many ticks without re-specifying an unchanged
  volume, relying on the AY register already holding the right value.
  `arkos_stop()` zeroes the real hardware but leaves the shadow holding
  the OLD pre-stop volume, so `arkos_ay_write_if_changed()` sees
  `shadow == target` (both still the old value, as far as it knows) and
  skips the rewrite on the first tick after resuming -- the channel would
  stay silently stuck at 0 until some later tick happens to change that
  channel's volume explicitly, which could be many ticks away. Use
  `arkos_pause()`/`arkos_resume()` instead: `arkos_pause()` snapshots each
  channel's current volume AND zeroes the shadow to match the silence (so
  nothing gets confused about what's "unchanged"), `arkos_resume()`
  restores the exact pre-pause volumes into both the hardware and the
  shadow in one step. Playback position (Linker/Track/RegisterBlock
  pointers, per-channel `rb_wait` countdowns) is untouched by either call
  -- it's `hrirq_stop()`/`hrirq_start()` (called separately, same
  convention as `arkos_stop()`) that actually freezes/resumes the passage
  of playback time, simply by not calling `arkos_tick()` at all while
  paused. Net effect: the held note/pattern resumes exactly where it left
  off, at exactly its pre-pause volume, with no discontinuity.

## Format, precisely traced from `akyplayer.s`

- **Header**: byte 0 = format version (unused by the player, just
  skipped); byte 1 = total channel count; then one 4-byte skip per group
  of 3 channels (this player only supports a single PSG group, matching
  `akyplayer.s`'s own scope -- "This player only target 1 PSG"). Lands on
  the **Linker**'s own start address.
- **Linker** (pattern order list): a sequential array of 8-byte entries:
  `[duration_lo, duration_hi, track1_ptr_lo, track1_ptr_hi, track2_ptr_lo,
  track2_ptr_hi, track3_ptr_lo, track3_ptr_hi]`. All pointer fields are
  **absolute 16-bit addresses** (see "Why this format needs a fixed load
  address" above). A `duration == 0` entry means "end of song", and
  *repurposes* its own `track1_ptr` field pair as an absolute pointer to
  loop back to.
- **Track** (per-channel, one pattern's worth of frames): a sequential run
  of 3-byte triples `[duration, registerblock_ptr_lo,
  registerblock_ptr_hi]`, each held for `duration` frames before advancing
  to the next triple. Triples' durations sum to exactly the owning
  pattern's own duration -- the natural, storage-driven stopping rule (no
  explicit end marker needed). **Tracks are frequently shared/reused**
  across different Linker entries when their content is identical (a real,
  common compression technique in real modules, not a rare edge case).
- **RegisterBlock**: the actual per-frame AY register payload -- a
  compact, bit-packed, *stateful* encoding. The FIRST frame of a given
  Track triple's RegisterBlock is decoded in "initial state" (fuller
  specification, matching a fresh note/instrument selection); every
  subsequent frame within the SAME triple's duration span is decoded in
  "non-initial state" (fewer bytes, delta-style), continuing from wherever
  the previous frame's own decode cursor advanced to -- NOT reset back to
  the triple's own base pointer. Four sound-type modes, each with its own
  bit layout, both for initial and non-initial state (8 decode paths
  total): no-software/no-hardware (pure fixed volume, optionally noise),
  hardware-only (AY envelope-driven amplitude), software-only (plain tone +
  volume), and software-and-hardware (tone period sent alongside
  envelope-driven amplitude -- the classic AY "envelope bass"/bell-like
  timbre). **RegisterBlocks contain no embedded pointers** -- pure inline
  value data (volume nibbles, tone/hardware-period bytes, noise index,
  envelope nibble).
- **Mixer register (AY reg 7)** is built incrementally across the 3
  channels via a shared accumulator (`r7`, starting at `$E0`) that each
  channel's own decode ORs/ANDs into (closing its own tone bit, opening its
  own noise bit), then shifts right by 1 before the next channel's turn --
  the same general incremental-mixer-construction technique this project's
  own (now-archived) PT3 player used for the same register, independently
  traced from a different reference (`ppt3.s`)'s `CH_MIX`/`CH_EXIT`.

### A precise, load-bearing subtlety: `SoftwareAndHardware`'s non-initial frequency-register bookkeeping

`akyplayer.s`'s non-initial `SoftwareAndHardware` decode path (around its
own `PLY_AKY_RRB_NIS_SAHH_AFTERMSBS` label) does something that looks, on
a first read, like it should double-increment the shared frequency-register
index (`X` in the reference): it does `INX` immediately before sending the
software-period MSB (so the write lands on `freq_reg+1`, the correct MSB
register), then `DEX` immediately after -- undoing it -- before an
unconditional `INX INX` a few lines later. Net effect: **zero** change to
the persistent register-index bookkeeping from the LSB/MSB handling itself;
the ONLY actual advance is the later unconditional `+2`. Missed on a first
attempt at a Python decode replica (which double-counted this as `+1` then
`+2`, an off-by-one that would have corrupted every subsequent channel's
own register indices for any module using this specific mode) -- caught by
re-reading the exact instruction sequence a second time rather than trusting
a first paraphrase, and worth calling out here since it's exactly the kind
of subtle bookkeeping bug this project's own PT3 investigation ran into
repeatedly with different mechanics.

## What's precisely traced vs. standard-semantics

Given the scope of a bit-exact port, this rewrite deliberately combines two
things, each clearly delineated:

- **Precisely traced from `akyplayer.s`** (verified by hand, then
  cross-checked with a from-scratch Python decode replica against real
  file bytes): header layout, Linker/Track/RegisterBlock structure and
  stopping rules, all 8 RegisterBlock decode paths (4 sound-type modes ×
  initial/non-initial), the mixer-register incremental construction, and
  the frequency-register bookkeeping subtlety above.
- **Not yet implemented / known gaps** (real limitations, not silently
  risked):
  - Multiple PSG support (`akyplayer.s`'s own header-skip loop already
    only targets a single PSG group; this player doesn't extend beyond
    that either).
  - No verification yet against a real multi-song corpus beyond the one
    module actually shipped with this demo -- expect real decode bugs to
    surface via emulator-capture verification the same way PT3's did,
    across several rounds, not assume the first working build is bug-free.

## Verification

Same discipline as PT3: a from-scratch Python decode replica, run against
real `.aky` file bytes, validated the header/Linker/Track structure and
every RegisterBlock decode path *before* any C was written. Real emulator
(Phosphoric) RAM-dump captures of `arkos_debug_shadow()` across a real
stretch of playback, on both distribution targets, confirm sane, varied AY
register output -- the user's own listening test via `make
run-phos`/`make run-disk` remains the final word on whether the music
actually sounds right, same as every audio change to this project.

### Real bug found and fixed: `SOFTWAREONLY` vs `SOFTWAREANDHARDWARE` dispatch inverted

After the initial implementation shipped (synthetic `arkos_test.aky` fixture
green, both targets stable), the user reported real playback was audible
but "nonsensical notes." The synthetic fixture only ever exercised the
`NoSoftNoHard`/`NoSoftNoHard-or-loop` paths (deliberately, to keep it a
simple straight-line decode) -- `SOFTWAREONLY`, `SOFTWAREANDHARDWARE`, and
`HARDWAREONLY`, the paths that actually write tone-period registers, were
never validated against real data before this.

**Diagnosis**: wrote a second, more thorough Python replica -- this time
simulating the *entire* player (3-channel loop, Linker/Track/RegisterBlock
state machine, not just one channel's own track) against the real shipped
`assets/steppingout.aky`, run for 3000 ticks (~60 simulated seconds), and
scanning every channel's own decode pointer for values landing outside the
file's own address range. Found channel B's decode pointer periodically
jumping to nonsensical addresses (e.g. `$0806`) -- always immediately after
two consecutive `SOFTWAREANDHARDWARE`-non-initial decodes triggered the
RegisterBlock format's own real "loop" mechanism (a frame legitimately
pointing at a different block address) with a target pointer that made no
sense for this file. This meant the stream was genuinely misaligned by the
time the "loop" fired -- a real trigger, but on the wrong byte, cascading
into reading essentially random memory as music data (explaining both
"nonsensical notes" and, eventually, decode corruption entirely).

Re-fetched `akyplayer.s` directly and traced the *exact* 6502 branch
semantics of its `BCC label / JMP target / label:` long-branch idiom (used
throughout the RegisterBlock dispatch instead of a direct `BCS`/`BCC` when
the target is too far for a short branch): carry **SET** falls through to
the `JMP` (goes to `target`); carry **CLEAR** branches over the `JMP` to
`label` (falls through to whatever comes *after* the whole block). This is
the *opposite* polarity from a same-named direct branch instruction, and
three of `arkos.c`'s dispatch conditions had it backwards -- inverted
during the original port, and never caught because they cancelled out
correctly in isolation for the specific byte values the synthetic fixture
happened to use:

1. `arkos_rb_initial()`'s top-level dispatch (`if (r->c == 0)` for entering
   `SOFTONLY_OR_SOFTANDHARD` -- should be `if (r->c == 1)`).
2. `arkos_rb_initial()`'s nested `SOFTWAREANDHARDWARE`-vs-`SOFTWAREONLY`
   check (same inversion).
3. `arkos_rb_noninitial_from_byte()`'s nested `SOFTWAREANDHARDWARE`-vs-
   `SOFTWAREONLY` check (same inversion; the non-initial dispatch's
   top-level check already used a direct `BCS` and was correctly `if (r->c
   == 1)` from the start).

**Fix**: flipped all three conditions to `if (r->c == 1)`. Re-ran the
full-player Python simulation against the real song: **0 anomalies** (down
from 170 out-of-bounds pointer excursions) over the same 3000-tick window,
and the reconstructed tone-period sequence changed from wildly jumping
values (e.g. 3937 -> 1121 -> 97 -> 0 -> 47) to a musically coherent,
repeating pattern (alternating 95/47 -- an octave relationship). The
synthetic `tests/fixtures/arkos_test.aky` fixture's own byte values had to
be regenerated (the same bytes now decode through *different* branches
under the corrected conditions) -- see `src/buildtest.c`'s own comment for
the new expected values. Verified stable over a real 60M-cycle Phosphoric
run of the actual demo (tape target) with the corrected code, no crashes,
bird/background still rendering correctly.

**Lesson for any future change to this dispatch tree**: the `BCC label /
JMP target / label:` idiom's polarity is easy to misread once, and -- as
happened here -- a single self-consistent misreading survives a
from-scratch Python replica built from the *same* misreading, and survives
a synthetic test fixture that happens not to exercise the affected branch.
Validating a decoder's *internal* consistency (C matches its own Python
replica) is necessary but not sufficient; the real check is decoding *real
file bytes* end-to-end and confirming the results make sense (in-bounds
pointers, plausible tone periods), not just "does it match my own model of
the format."

### Second real bug found and fixed: the mixer shift-accumulator ran one shift too many

After the dispatch-inversion fix above, the user reported real playback was
"almost there" but the third channel's own line never seemed to appear.
Extended the full-player Python simulation to run a whole song loop
(~12000 ticks/240 seconds for `steppingout.aky`) and track each channel's
own tone-enabled/noise-enabled/volume state over that whole window.
Channel 3 (index 2) showed `tone_enabled` **0 out of 20000** simulated
ticks and `noise_enabled` **20000 out of 20000** -- stuck permanently
routed through the noise generator instead of its own tone, for the
*entire* song, while channels 1 and 2 showed normal, varied tone/noise
mixes correlated with their own volume activity. This exactly explains
"the third channel line never appears": it was there and had real,
varying volume/note data, but always rendered as noise texture rather than
a melodic voice.

**Root cause**: `arkos.c`'s `r7` mixer accumulator (see the Format section
above for the shift-accumulator technique) shifted `r7` right by one
**unconditionally after every channel**, including the third. Re-checked
`akyplayer.s`'s own `PLY_AKY_PLAY` directly: it does `LSR r7` between
channel 1 and channel 2, then `ROR r7` between channel 2 and channel 3, and
critically **no shift at all after channel 3** -- exactly 2 shifts for 3
channels, not one per channel. Each channel's own tone/noise decision is
written to a *fixed* bit position (bit 2 for tone, bit 5 for noise) during
its own turn; the between-channel shifts are what walk each channel's own
bit down into its final, correct position in the real AY mixer register
(bit 0/3 for channel A, bit 1/4 for channel B, bit 2/5 for channel C). An
extra, unwanted 3rd shift pushes channel 3's own bits one position too far
and simultaneously overwrites them with whatever channel 1's own bits had
already been shifted into by that point -- since channel 1's tone
happened to stay closed almost all the time in this song's own bassline
instrument, channel 3's "tone" bit read as permanently disabled.

**Fix**: guard the shift with `if (i < 2)` in `arkos_tick()`'s 3-channel
loop, so it only fires between channels, matching the reference exactly.
Re-ran the full-song simulation: channel 3's `tone_enabled` count now
tracks its own volume activity exactly (18848/20000 ticks, matching
18848/20000 nonzero-volume ticks precisely), the same clean correlation
channels 1 and 2 already showed. The synthetic fixture's expected mixer
value also changed (`0x1F` -> `0x3F`, since all 3 of its channels always
close their own tone and never open noise -- with the shift fixed, all 6
tone+noise bits correctly end up disabled instead of a scrambled partial
result). Verified stable over another real 60M-cycle Phosphoric run, no
crashes, bird/background still correct.

**Lesson, same theme as the first bug**: a mixer register built via a
shift-accumulator across multiple channels can be "self-consistently
wrong" -- every individual channel's own write looks locally plausible,
and even a hand-built synthetic fixture (which only ever computes ONE
final mixer value per tick, not a per-channel breakdown) won't expose a
bit-position scramble unless you specifically check that a channel's own
*known* activity (its volume) correlates with its own tone/noise-enable
bits over an extended, realistic window -- exactly what following one
song's structure for its FULL loop, not just a handful of ticks, made
visible.

### Third real bug found and fixed: the mixer write silently disabled keyboard scanning

Found while investigating a user report that pressing a key never skips to
the next demo section. `arkos_tick()`'s own mixer write masked `r7` with
`& 0x3F` before sending it to the hardware -- correct for the tone/noise
enable bits (0-5), but this unconditionally zeroes bits 6-7 too. AY-3-8912
register 7 bit 6 controls Port A's I/O direction: bit 6 = 1 is required
for `include/keyboard.c`'s `keyb_scan()` to sense keypresses at all (this
project's own LOCI boot path pre-seeds register 7 to `$7F` for exactly
this reason -- see `include/loci.c`). Since `arkos_tick()` runs every
single tick from ~20ms after boot for the rest of the program's life, this
silently forced Port A into output mode and permanently broke keyboard
scanning almost immediately after startup, for effectively the entire
real runtime of every session (music is always playing). **Fix**: write
`(uint8_t)((r7 & 0x3F) | 0x40)` instead of `(uint8_t)(r7 & 0x3F)` --
keeps the correctly-computed mixer bits while unconditionally forcing bit
6 back to input mode on every write, so keyboard scanning keeps working
throughout playback. `tests/scripts/test_boot.sh`/`test_disk.sh`'s
hardcoded "Arkos AY registers" expected-dump strings needed updating
(register 7: `3F` -> `7F`) since they were asserting the old, broken
value.

**Diagnostic note**: confirmed via a temporary debug probe that the fixed
value (with bit 6 set) really does reach the AY shadow register (and thus
the real hardware write) -- the mixer-register mechanism itself is
confirmed correct. Headless Phosphoric `--type-keys` testing still could
not independently confirm the keypress-detection symptom is fully
resolved (a separate, unresolved mystery in the test harness itself, with
several plausible explanations investigated and ruled out -- see the
plan file's own Round 5 write-up for the full list) -- this is a real,
hardware-accurate fix regardless, and should be verified with a real
interactive session (`make run-phos`/`make run-disk`) rather than trusted
on headless testing alone.
