# PT3 music player (pt3.h)

Plays PT3 (Vortex Tracker / "ProTracker 3") AY-3-8912 tracker modules,
ticking via [rasterirq.h](rasterirq.md) and loading tune data at runtime
from [LOCI](loci.md) mass storage. Include `pt3.h`; it auto-compiles
`pt3.c` via `#pragma compile`.

## Attribution

Original Z80 PT3 replay algorithm by S.V.Bulba (c)2004,2007, with Ivan
Roshin for the note/volume table generators
([bulba.untergrund.net](https://bulba.untergrund.net/main_e.htm)). 6502 port
for the Oric ("Vortex Tracker II v1.0 PT3 player for 6502") by ScalexTrixx
(A.C), (c)2018, MIT-licensed, in
[6502Nerd/dflat](https://github.com/6502Nerd/dflat) (`Oric` branch,
`Oric/software/project/pt3/ppt3.s`).

**This module is a from-scratch C rewrite, not a transliteration** — see
"What's precisely traced vs. standard-semantics" below for exactly which
parts came from reading that source directly (line-by-line, with the
dispatch arithmetic hand-verified) vs. which parts use the well-established,
cross-platform PT3 algorithm/format conventions instead of replicating that
one 6502 implementation's specific hand-optimized instruction sequences.

**Demo music**: `assets/oxygene4.pt3`, used by `src/main.c`, is also from
`6502Nerd/dflat`'s own `Oric/software/project/pt3/tunes/` collection (same
MIT-licensed repo as the player above) — a chiptune cover of Jean-Michel
Jarre's "Oxygène IV". Used deliberately, understanding it's a cover of a
commercial track (common, low-risk-in-practice demoscene convention for
personal/non-commercial use — nearly every tune in that same `tunes/`
collection is a cover of a well-known commercial track), distinct from the
repo's own MIT-licensed code.

## API

```c
bool pt3_load(const char *path);
void pt3_init(void);
__interrupt void pt3_tick(void);
void pt3_stop(void);
const uint8_t *pt3_debug_shadow(void);
```

`pt3_load(path)` loads a module from LOCI storage into a fixed
`PT3_MAX_MODULE_SIZE` (6144 bytes, adjustable) static buffer — no heap.
Returns `false` (silent no-op, no music, not a crash) if LOCI isn't present,
the file doesn't exist, or it's too large.

`pt3_init()` resets playback to the start of the loaded module — order
position, per-channel state, tempo counter — and **reprograms VIA Timer 1's
latch to a true 50Hz value** (`TIMER1_50HZ`, `oric.h`), since PT3 ticks at
50Hz conventionally, while Timer 1 is otherwise left at the ROM-inherited
100Hz free-run rate. This affects **any other [rasterirq.h](rasterirq.md)
consumer sharing the same timer** — a raster-split effect scheduled via
`hrirq_add()` alongside a PT3 player gets a full frame's cycle budget to
spread its callbacks across (50Hz) rather than half a frame (100Hz), which
is arguably the more useful default, but is a real, project-wide-affecting
change once both features are used together. Call once after a successful
`pt3_load()`.

`pt3_tick()` is one playback tick: decodes any pattern rows that are due,
steps ornaments/samples, computes all 14 AY register values, and writes
them via [`ay_write()`](ay.md). **Already `__interrupt`-qualified** — wire
it up via `hrirq_add()`:

```c
pt3_load("music.pt3");
pt3_init();
hrirq_init();
hrirq_add(100, pt3_tick);   // cycle_offset: see rasterirq.md's calibration note
hrirq_start();
```

`pt3_stop()` silences all 3 channels (zero amplitude) without touching
`hrirq` state — call `hrirq_stop()` separately to stop `pt3_tick()` firing
at all.

`pt3_debug_shadow()` returns the last-computed 14 AY register values —
**testing only**, not needed by normal playback code (see "Verification"
below for why this exists).

## Format, precisely traced from `ppt3.s`

A module's header (byte offsets relative to its own load address) holds a
tempo byte (offset 100, ticks/row), an order-list loop-back index (offset
102), a 16-bit little-endian offset to the patterns table (offset 103-104),
and fixed-offset pointer tables: 32 samples (offset 105, 2 bytes/entry) and
16 ornaments (offset 169, 2 bytes/entry). The order list itself (offset 200)
is a byte stream of pattern indices, `0xFF`-terminated, looping back to the
offset-102 index.

**A load-bearing quirk, confirmed by hand-tracing `ppt3.s`'s `PLAY`
routine**: each patterns-table entry is 6 bytes (3 channel stream offsets,
2 bytes each), but the code indexes it as `patterns_table + order_byte*2` —
this only works because the order-list bytes stored in a real `.pt3` file
are *already* `pattern_number*3`, not a plain 0-based index. Getting this
wrong (using `*3` or a plain index) makes every pattern after the first
play garbage.

**Pattern command-byte dispatch** — derived by hand-tracing `ppt3.s`'s
`PD_LOOP` carry-chain arithmetic precisely (not assumed from generic PT3
documentation, though it matches it):

| Range | Meaning |
|---|---|
| `0x00-0x0F` | 16 special commands (table below) |
| `0x10-0x1F` | envelope shape (0-15) + sample select (next byte -- see note below) |
| `0x20-0x3F` | noise period (0-31) |
| `0x40-0x4F` | ornament select (0-15) |
| `0x50-0xAF` | note (0-95) — ends the row |
| `0xB0` | envelope off |
| `0xB1` | row-hold count in the next byte |
| `0xB2-0xBF` | envelope select (shape 1-14 + 16-bit period, next 2 bytes) |
| `0xC0` | note release/off — ends the row |
| `0xC1-0xCF` | volume set (1-15) |
| `0xD0` | mid-stream "end this row, no note" — ends the row |
| `0xD1-0xEF` | sample select (1-31) |
| `0xF0-0xFF` | ornament select (0-15) + sample select (next byte -- see note below) |

**A real, confirmed decode bug lived in the two "sample select (next
byte)" rows above** (`0x10-0x1F` and `0xF0-0xFF`): the stream byte there
is NOT a plain 0-31 sample index like `0xD1-0xEF`'s own `cmd-0xD0` is —
`ppt3.s`'s `PD_ESAM` (`0x10-0x1F`'s handler) reads it and jumps straight
into `PD_SAM_` with no transformation at all, and `PD_OrSm` (`0xF0-0xFF`'s
handler) reads it, does `lsr a; bcc +; ora #$80; +: asl a` (which nets out
to just clearing bit0 — a defensive "force even" step, NOT a doubling)
before falling into the same `PD_SAM_`. Either way, the raw byte already
IS the byte *offset* into the sample-pointer table (i.e. `index*2`), not a
plain index that still needs doubling. An earlier draft here stored the
raw byte directly as `sample_num` (a plain index elsewhere in this code)
and then doubled it AGAIN at the `sam_data` lookup — silently reading the
WRONG sample-pointer-table slot for every combined-select command. In the
real `assets/oxygene4.pt3` module this was catastrophic for one channel:
its combined orn+sample command always specified byte `18` (meaning index
`9`, a real, defined sample), but the doubling bug read index `18` instead
— an UNDEFINED slot in that module's own sample-pointer table — which
`pt3_channel_tick()`'s `sam_data==0` handling then silences for the tick.
Since that channel relied on this command throughout, it was **completely
silent for the entire song**, not just quieter — the other two channels
also hit undefined slots this way for roughly 10-18% of their own active
ticks. Fixed by halving the raw byte (`(byte & 0xFE) >> 1`, the mask being
a cheap, harmless defensive echo of `PD_OrSm`'s own bit0-clear) before
storing it as `sample_num`, restoring the same "plain index" meaning the
`0xD1-0xEF` path already had. Verified via a from-scratch Python replica
of the fixed decode logic run against the real module: 0% undefined-slot
hits across all three channels over 6000 simulated ticks (previously
17.7%/9.5%/**100%**).

A **row can carry several of these commands** (e.g. volume, then an
ornament+sample select, then a note) before it ends — `pt3_decode_row()`
loops over `pt3_decode_command()` until a row-ending command is hit,
matching `PD_LOOP`'s own structure. An earlier draft of this decoder
processed exactly one command per tick, which desyncs multi-command rows;
worth calling out since it compiles fine and only shows up as wrong
playback.

**16 special commands** (`0x00-0x0F`, `ppt3.s`'s `SPCCOMS` jump table,
traced directly): `0`=no-op, `1`=glissando, `2`=portamento, `3`=sample
position, `4`=ornament position, `5`=vibrato on/off, `6`,`7`=no-op,
`8`=envelope glide, `9`=tempo set, `10-15`=no-op. All except `6`/`7`/`10-15`
(no-ops by design) are implemented — see "Effects" below for glissando/
portamento/vibrato/envelope-glide's specific design (a standard-semantics
implementation, not a bit-exact replica of `ppt3.s`'s own bookkeeping for
these four).

**Channel-A-gated pattern advance**: only channel A's stream is checked for
a literal `0x00` byte (distinct from command byte `0x00`, "special command
0/no-op", which can appear mid-row via the dispatch table above) as the
signal "this pattern is exhausted." When hit, the order list advances (with
loop-back at `0xFF`) and **the new pattern's first row is decoded in the
same tick** — deferring it to the next tick (an earlier draft's bug) leaves
the row-hold counter at 0, underflowing to 255 on the next tick's
decrement.

**Ornament/sample stepping**: each has a loop-index byte + length byte,
then step data. The step used for a given tick is read *before* advancing
(with wraparound to the loop index at the length boundary) — "use old, then
advance", not "advance, then use." Ornament steps are a signed 1-byte tone
offset; sample steps are 4 bytes: a flags byte, a mix-flags byte, and a
16-bit signed tone delta.

Each sample step's two flag bytes were fetched directly from `ppt3.s`
(`CH_AMP`/`CH_NOAM`/`CH_MIX`/`CH_EXIT`, the code that loads them into Z80
registers `z80_C` (first byte) and `z80_B` (second byte)) and their real
roles are:

The **flags byte** (`sam_flags`, `z80_C`): **bit 7 enables the persistent
per-channel amplitude slide (`CrAmSl`/`amp_slide`), bit 6 picks its
direction** (1=up, 0=down) — see "Effects" below for `CrAmSl` itself. Bit 0
gates a per-step envelope-enable check in `ppt3.s` (`CH_ENV`) that this
implementation doesn't currently read (envelope is only ever enabled via
the `0xB2-0xBF` command, not per-sample-step — a real, minor scope gap, not
yet a reported symptom). This byte's low nibble is **not** an amplitude
value — see the note below on the amplitude nibble's real source, an
earlier bug in this exact area.

**Two further known gaps, found by the same Opus-model cross-reference
audit that found bug #3 below, confirmed against `ppt3.s` AND deater's
`vmw-meter/ay-3-8910/pt3/pt3_lib.c`, but NOT yet fixed** (neither is the
cause of any currently-reported symptom — `oxygene4.pt3`'s own
noise-enabled sample steps all happen to carry a zero offset, so these
gaps are latent for this specific song, not necessarily for others):
- **No per-step noise-period offset.** Both references add a small offset
  (derived from `sam_flags`'s upper bits, `ppt3.s`'s `AddToNs`) to the
  shared noise-period register on any step with noise enabled — used for
  "tuned noise" effects (a snare/hi-hat sample deliberately detuning the
  shared noise generator per step). `pt3_tick()` here just writes
  `pt3_noise_period` directly, ignoring any such offset.
- **No per-step envelope-period slide.** Symmetric to the effect-level
  envelope-glide (`C_ENGLS` special command 8, already implemented —see
  "Effects" below): both references also let a sample step (`sam_flags`
  bit7) feed its own accumulator into the shared envelope period
  (`ppt3.s`'s `AddToEn`). Only matters for envelope-driven instruments;
  none of `oxygene4.pt3`'s currently-used samples enable this.

The **mix-flags byte** (`sam_mixflags`, `z80_B`): **bits 0-3 are the
amplitude nibble** (0-15, combined with the channel's own volume via
`PT3_VOLUME_TABLE` — see "What's precisely traced" below); **bit 4 is the
tone mask, bit 7 is the noise mask for the AY mixer register — both
active-low (0 = enabled), evaluated fresh every tick from the CURRENT
step**; **bit 6 controls whether the tone delta *accumulates*** into the
channel's running total (persists across ticks — a gliding/vibrato-via-
sample effect) or is applied fresh each step without carrying forward.

**Three real, confirmed bugs have lived in this exact area, across three
separate rounds of debugging:**

1. An earlier draft set a per-channel `noise_enabled` flag to `true` the
   first time ANY row used the noise-*period*-select command (`0x20-0x3F`,
   which only sets the AY's single shared noise-period register, `Ns_Base`
   in `ppt3.s` — it has nothing to do with any one channel's own mixer
   enable bit) and never reset it, permanently mixing the noise generator
   into that channel's output for the rest of the song regardless of what
   the current sample step actually wanted. Fixed by deriving
   `out_tone_on`/`out_noise_on` fresh every tick from the current step's
   bit4/bit7 (initially read from the wrong byte, `sam_flags` — see next
   point — then corrected to `sam_mixflags`, matching `ppt3.s`'s own
   `CH_MIX`: `lda z80_B / ror a / and #$48`, which rotates `z80_B`'s bit4/
   bit7 into position before OR-ing them into the shared mixer accumulator;
   this exact bit-position/channel-order combination, verified by
   simulating `CH_MIX`/`CH_EXIT`'s incremental construction against the
   AY's real bit0-2=tone/bit3-5=noise layout, is the only one that
   reproduces it).
2. **The amplitude nibble was read from the wrong byte entirely**: an
   earlier draft (added alongside implementing `CrAmSl`, below) computed it
   as `sam_flags & 0x0F` — reasoning that since `sam_flags`'s bit7/bit6
   drive the slide, its low nibble must be the value being slid. Fetching
   `ppt3.s` directly and reading `CH_NOAM` line-by-line shows this is
   wrong: `lda z80_B / and #15 / adc z80_L` (`z80_L` holding `CrAmSl`) —
   the nibble comes from `z80_B` (`sam_mixflags`), the *second* byte, not
   `z80_C` (`sam_flags`), the first. `sam_flags`'s own low nibble is never
   read as an amplitude anywhere in `ppt3.s`. Confirmed against
   `assets/oxygene4.pt3`'s own sample 15 (a single-step noise/percussion
   sample): `flags=0x01, mixflags=0x1F` — the old code read amplitude=1
   (near-silent at *any* channel volume, since `PT3_VOLUME_TABLE`'s
   low-amplitude columns stay near 0 regardless of the volume row) where
   the corrected byte gives amplitude=15 (full-scale, scaling properly with
   channel volume). This was a real, confirmed, direct cause of "overall
   volume stays low throughout the song" — affecting every sample in the
   module that uses a low nibble in `sam_flags`, not just this one, since
   the wrong byte was read on every tick, every channel. Fixed by changing
   the combine to `sam_mixflags & 0x0F`. `tests/fixtures/music.pt3`/
   `music_effects.pt3`'s own sample 0 had its test amplitude (`0x0F`)
   hand-placed in the flags byte to match the *old* (wrong) behaviour;
   both fixtures' sample-0 step-0 flags/mixflags bytes were swapped to keep
   testing the same volume-table combine under the corrected byte source
   (see `tests/scripts/test_boot.sh`'s own comment on this).
3. **A note strike didn't reset `sample_pos`/`ornament_pos`.** `ppt3.s`'s
   `PD_NOTE` falls through to `PD_RES`, which zeroes a whole block of
   per-channel state on every fresh note — including byte 0 (`PsInOr`,
   ornament position) and byte 1 (`PsInSm`, sample position), not just
   `tone_acc`/`CrAmSl` (already reset here, see their own comments above).
   An independent reference (deater's `vmw-meter/ay-3-8910/pt3/pt3_lib.c`)
   agrees: its own note-command handler explicitly zeroes both positions
   too. Without this, a channel that selects its sample/ornament ONCE via
   a combined command and then re-strikes plain notes for the rest of a
   pattern — a completely ordinary way to write a repeating bassline —
   never restarted its sample's own envelope on those later strikes:
   `sample_pos` just kept advancing/looping from wherever it already was,
   so every note after the first played whatever step the sample happened
   to be looping on (often a quiet late/loop step) instead of the sample's
   own attack transient. Confirmed against `assets/oxygene4.pt3`'s own
   channel C (a fast bassline using sample 9, a 7-step plucked instrument
   with amplitudes `11,11,9,7,6,6,6` looping back to step 4): before the
   fix, the AY volume register settled into the loop's low steps (~4-6)
   and stayed there for the whole pattern; after the fix, it clearly
   re-attacks (e.g. `0x0C → 0x0A → 0x07 → 0x06 → 0x05`, then straight back
   up to `0x08`+ on the next note) — the difference between a flat drone
   and a rhythmically plucked bassline. Found via a dedicated Opus-model
   audit cross-checking `pt3.c` against `ppt3.s` plus two independent
   platform references, specifically asked for after fix #2 above resolved
   the general "volume too low" complaint but one channel's bassline still
   didn't sound right. Fixed by adding `chan->sample_pos = 0;
   chan->ornament_pos = 0;` to the note handler (covering both the plain
   and portamento branches); also reset `vibrato_counter`/`vibrato_audible`
   there for the same reason (`ppt3.s`'s `PD_RES` zeroes `COnOff` too) —
   vibrato pulsing should start fresh on a new note, not resume mid-cycle
   from the previous one.

## Effects: glissando, portamento, vibrato, envelope-glide

These four use a **standard, musically-correct implementation**, not a
bit-exact replica of `ppt3.s`'s own `CrTnSl`/`TnDelt` bookkeeping — that
mechanism relies on a self-modifying-code snapshot of the note field taken
at the start of each row (`PrNote`) combined with a sign-swap-based
distance comparison in `CHREGS`, and re-deriving it exactly turned out to
be underdetermined from static reading alone (it appears to require the
channel's `Note` field to be simultaneously "the old note" for the slide
calculation and "the new note" for `PD_NOTE`'s unconditional overwrite,
which isn't resolvable without live single-step debugging). Instead, this
implementation achieves the same *musical* result — slide from the source
pitch to the target pitch, or slide forever with no target — via a
self-consistent, independently-verified mechanism:

- **Portamento** (`C_PORTM`, cmd `2`, 5 bytes: delay, 2 reserved/legacy
  bytes unused by this format version, then a 16-bit signed step
  magnitude): sets up `slide_delay`/`slide_step` immediately, but defers
  computing the actual slide distance — the *target* note doesn't arrive
  until the row's own note command runs later in the same row. When that
  note command executes, if a portamento is pending: the channel's `note`
  field (still the *old*, pre-row value, since nothing else in this row
  has touched it yet) and the new note give
  `slide_target_dist = NT_[new] - NT_[old]`; the step's sign gets
  corrected to match that direction; `tone_slide` starts at 0 and
  accumulates by `slide_step` every `slide_delay` ticks until it
  reaches/passes `slide_target_dist` (checked sign-aware, since the
  distance can be positive or negative), at which point the channel snaps
  to the target note and the slide stops. `chan->note` is deliberately
  **not** updated when the portamento is set up — only on arrival — so
  `pt3_channel_tick()`'s tone lookup (`NT_[note] + tone_slide`) smoothly
  interpolates between the two notes throughout.
- **Glissando** (`C_GLISS`, cmd `1`, 3 bytes: delay, then a 16-bit signed
  step applied directly, no distance/target computation) reuses the exact
  same `tone_slide` accumulator and stepping logic, just with no target —
  it slides forever until cancelled by another command (a plain note
  strike, or vibrato).
- **Vibrato** (`C_VIBRT`, cmd `5`, 2 bytes: on-duration, off-duration) toggles
  the channel's audible/muted mixer state every `on_duration`/`off_duration`
  ticks (reloading from whichever duration corresponds to the new state) —
  a pulsing on/off amplitude effect, distinct from a true pitch-vibrato.
  Cancels any active slide (matches `ppt3.s`).
- **Envelope-glide** (`C_ENGLS`, cmd `8`, 3 bytes: delay, then a 16-bit
  signed step) is a **shared, not per-channel**, effect — the AY has only
  one envelope generator — gradually sweeping the shared envelope period
  every `delay` ticks, applied once per `pt3_tick()` regardless of which
  channel issued the command.

A fresh, plain note (no portamento pending) cancels any previously-active
slide — a newly struck note should sound at its own pitch, not continue an
old glide.

**A real robustness lesson from building this**: an earlier draft of this
project's own effects test fixture gave one channel a release command with
no row-hold, and — since every channel is checked for a new row every
tick when the module's tempo is 1 tick/row — the *next* tick immediately
tried to decode another row, read a stray zero byte (dispatched as a
harmless no-op special command, not a row terminator), and kept reading
forward through zero-filled buffer space indefinitely, since nothing in
that data ever produced a row-ending command. `pt3_decode_row()` now caps
itself at `PT3_MAX_COMMANDS_PER_ROW` (32) as a safety valve against exactly
this failure mode in any malformed or truncated real `.pt3` file (an
external, untrusted input) — not a hypothetical concern, since it happened
during development of this very feature.

## What's precisely traced vs. standard-semantics

Given the scope of a bit-exact port (~2400 lines of hand-optimized,
Z80-register-emulated 6502 assembly), this rewrite deliberately combines two
things, each clearly delineated:

- **Precisely traced from `ppt3.s`** (verified by hand, not assumed):
  header layout, the order-list `*2`/pre-scaled-by-3 mechanism, the
  command-byte dispatch ranges, all 16 special-command meanings, the
  channel-A-gated end-of-pattern advance, ornament/sample table structure
  and the "use old step, then advance" wraparound rule, and the envelope
  shape write-gating (`ROUT` only writes AY register 13 when a command set
  a new shape that tick — writing it every tick restarts the AY envelope
  generator needlessly).
- **Standard PT3 semantics, not `ppt3.s`'s exact algorithm** (a deliberate,
  user-approved scoping decision, not an oversight):
  - **Note table**: computed from the standard 12-tone-equal-temperament
    formula (`freq = 32.7032Hz * 2^(note/12)`, `period = AYclock/(16*freq)`)
    referenced against the ZX Spectrum's ~1.7734MHz AY clock (the
    convention PT3's note table assumes), rather than replicating
    `ppt3.s`'s own `NoteTableCreator` bit-shuffle generator. The result is
    a precomputed `const uint16_t[96]` table, matching this project's
    existing `fixedmath.c` convention for static lookup data.
  - **`FIX16BITS` clock correction** (289/512, ≈0.564×) *was* precisely
    traced from `ppt3.s`'s own `FIX16BITS` routine (`x256+x32+x1=x289`,
    then one more `/2` — a net `/512`, not `/256` as an earlier draft of
    this analysis mistakenly assumed before re-deriving it against real
    code) and baked into the precomputed note table at generation time.
    This matches physical reality: 289/512=0.5645 closely tracks an Oric
    AY clock roughly half its 1MHz CPU clock against the Spectrum's
    1.7734MHz reference (0.5639) — sanity-checked, not just asserted.
  - **Volume/amplitude scaling**: NO LONGER a scope cut as of the
    noise/volume investigation below — `PT3_VOLUME_TABLE` (in `pt3.c`) is
    `ppt3.s`'s own `VolTableCreator` 16×16 table, precisely re-derived by
    simulating its 6502 fixed-point generation algorithm in Python (not a
    linear approximation). An earlier draft used a simple linear combine
    (`(channel_volume * (sample_amplitude+1)) >> 4`) instead — measurably
    ~15-18% quieter on average across all 256 volume/amplitude
    combinations than the real table, a real, confirmed contributor to a
    "volume sounds too low" complaint. `ppt3.s`'s `CrAmSl` amplitude-slide
    mechanism is also implemented now (`amp_slide` in `Pt3Channel`,
    precisely traced from `CH_AMP`'s `cmp #15/beq CH_NOAM` and `cmp
    #$F1/beq CH_NOAM` clamping) — a persistent per-channel offset,
    accumulated ±1/tick whenever the current step's `sam_flags` bit7 is
    set (bit6 picks direction), added to the raw amplitude nibble before
    the table lookup. Getting **which byte** supplies that raw nibble was
    itself a second, separately-confirmed bug — see the format section
    above ("Three real, confirmed bugs") for the full story: it must come
    from `sam_mixflags`, not `sam_flags`.
  - **Mixer/noise bits**: NO LONGER a scope cut either — both tone-enable
    and noise-enable are derived fresh every tick from the CURRENT sample
    step's own mix-flags byte (bit4=tone mask, bit7=noise mask, both
    active-low), precisely matching `ppt3.s`'s `CH_MIX`/`CH_EXIT`
    incremental mixer-byte construction (verified by simulating its
    per-channel bit-extraction-and-rotate algorithm in Python against the
    AY's real bit0-2=tone/bit3-5=noise layout). Two earlier drafts got
    this wrong in two different ways — see the format section above for
    both.
  - **Effects (portamento, glissando, vibrato on/off, envelope glide)**:
    implemented (see "Effects" above), but via a from-first-principles
    design that produces the correct musical result rather than a
    bit-exact replica of `ppt3.s`'s own `CrTnSl`/`TnDelt`/`PrNote`
    bookkeeping, which turned out to be underdetermined from static
    reading alone (see that section for why).

## Verification

Phosphoric's `--dump-ram-at` dumps RAM only — the AY-3-8912's own internal
registers, and the VIA/PCR registers `ay_write()` drives them through, read
back as flat `0x00` regardless of what was written (confirmed empirically,
see [ay.md](ay.md)). **Audible correctness cannot be verified from a RAM
dump** — this project's test suite instead verifies the *computation* that
feeds `ay_write()`, via `pt3_debug_shadow()`.

`tests/fixtures/music.pt3` is a small, hand-built synthetic module (not a
real tune) exercising the core decode path: a volume+ornament/sample-select
+note sequence on channel A, a volume+note sequence on channel B, and a
release on channel C. `src/main.c` loads it, calls `pt3_init()` and one
`pt3_tick()` (direct call, not via `hrirq` — deterministic, not real-time),
and prints the 14-byte shadow array as hex; `tests/scripts/test_boot.sh`
asserts the exact resulting bytes (`79 07 BD 03 00 00 00 24 0F 0B 00 00
00`), hand-verified against the module's own crafted commands before being
locked in (byte 7, the mixer register, is `0x24` because the tone/noise
mixer-enable bits are derived from the current sample step's own flags byte
every tick, not a permanent per-channel latch -- a real, confirmed bug,
fixed after the noise-period-select command (0x20-0x3F) was found to be
setting a per-channel noise-enable flag that then never turned back off;
byte 9, channel B's volume register, is `0x0B` because `PT3_VOLUME_TABLE`
-- ppt3.s's own `VolTableCreator` table, precisely re-derived -- replaced
an earlier linear volume/amplitude combine that gave `0x0A` for this same
combination, a real, confirmed under-representation). This proves the header parsing, order-list/pattern-pointer
derivation, multi-command row decoding, ornament/sample stepping, note-table
lookup, amplitude scaling, mixer-bit computation, and envelope write-gating
all produce the exact expected values for this test case — real behavioral
verification, not just "compiles and doesn't crash."

`tests/fixtures/music_effects.pt3` is a second synthetic module exercising
the four effects across 5 ticks (tempo = 1 tick/row): channel A strikes
note 0, then portamento-slides toward note 12 (delay=2, step=50, held for
6 ticks via a row-hold command so the slide has room to run without
another row decode interrupting it); channel B strikes note 7 with vibrato
(on=2, off=3); channel A's first row also sets an envelope-glide (delay=2,
step=+10). Hand-computed tick-by-tick: channel A's tone goes
1913 → 1913 → 1863 → 1863 → 1813 (accumulating -50 every 2 ticks); channel
B's vibrato toggles inaudible at tick 2 and audible again at tick 5; the
shared envelope period sweeps 0 → 10 → 20. `src/main.c` runs 5 ticks and
prints the resulting shadow array; `tests/scripts/test_boot.sh` asserts
the exact bytes (`15 07 FD 04 00 00 00 24 0F 0B 00 14 00`) -- bytes 7/9
are `0x24`/`0x0B` for the same two reasons noted above.

**A real bug this test caught during development** (not a hypothetical):
the first draft of this fixture gave channel C a release command with no
row-hold; since every channel is checked for a new row every tick at this
module's 1-tick/row tempo, the very next tick tried to decode a second row
for that channel, read a stray zero byte as a harmless no-op (not a row
terminator), and kept reading forward through the module's zero-filled
tail indefinitely — eventually producing a bogus but plausible-looking
note from whatever garbage byte it happened to land on, with no crash or
compiler warning to flag it. This is exactly the class of bug the
`PT3_MAX_COMMANDS_PER_ROW` safety valve (see "Effects" above) now bounds,
and exactly why hand-verified, byte-exact assertions catch things a bare
"does it crash" smoke test would silently pass.

**Not verified by these tests**: multi-row/multi-pattern playback over
many tens/hundreds of ticks, order-list looping in practice, and anything
about how the result actually *sounds* on real hardware or in Oricutron
(which does emulate real AY audio, unlike Phosphoric, and is the right
tool for that kind of check).

`--loci-flash DIR` (Phosphoric) is required for `pt3_load()`'s `file_load()`
to actually succeed in tests — the bare `--loci` flag only emulates the
LOCI MIA hardware being *present* (what `loci_present()` checks), not a
mounted filesystem. `Makefile`'s `test-capture` target and
`tests/scripts/test_boot.sh` both mount `tests/sandbox` this way.
