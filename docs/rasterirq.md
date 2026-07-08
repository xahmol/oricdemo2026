# Raster IRQ / mid-frame effects (rasterirq.h)

Colour splits and raster bars via a self-contained VIA Timer 1 interrupt
handler. Both of this project's Oscar64 runtimes (`oric_crt.c`,
`oric_crt_hires.c`) leave interrupts permanently disabled (`SEI`, no
`CLI`) — `oric_crt.c`'s IRQ NOTE already anticipates this module as the
intended extension point ("a proper IRQ handler with full register
save/restore could be installed later if needed"). Include
`rasterirq.h`; it auto-compiles `rasterirq.c` via `#pragma compile`.

```c
typedef void (*RasterCallback)(void);

void hrirq_init(void);
uint8_t hrirq_add(uint16_t cycle_offset, RasterCallback cb);
void hrirq_start(void);
void hrirq_stop(void);
```

`hrirq_init()` installs the handler at `IRQ_VEC_LO`/`IRQ_VEC_HI`
([oric.md](oric.md), `$0245`/`$0246`) and clears the callback schedule.
**Does not enable interrupts** — call once before `hrirq_add()`/
`hrirq_start()`.

`hrirq_add(cycle_offset, cb)` schedules a callback to run during the IRQ
handler, after a busy-wait of approximately `cycle_offset` "delay units"
since the *previous* scheduled callback fired (or since Timer 1's IRQ
trigger, for the first call after `hrirq_init()`) — **not** an absolute
frame-relative offset. Up to `HRIRQ_MAX_CALLBACKS` (8) may be scheduled;
extra calls are silently ignored (check the return value). `cb` **must be
declared `__interrupt`** — see "Callback safety" below.

`hrirq_start()` enables interrupts (`CLI`) — the installed handler becomes
active and Timer 1's IRQ now triggers it. `hrirq_stop()` disables
interrupts (`SEI`) again — safe to call even if `hrirq_start()` was never
called, and restores this project's default IRQ-free state.

**Timer 1's rate is whatever it's currently programmed to**, not fixed by
this module. Out of the box (ROM boot, nothing else touched it) it's a
free-running 100Hz (`TIMER1_100HZ`, [oric.md](oric.md)). [pt3.md](pt3.md)'s
`pt3_init()` reprograms it to a true 50Hz (`TIMER1_50HZ`) to match PT3's
conventional tick rate — since there's only one practical free-running
timer and one IRQ vector on this hardware, **any `hrirq_add()` callback
scheduled alongside PT3 playback shares whatever rate PT3 set**, not the
100Hz this section originally assumed. A raster-split effect's
`cycle_offset` values need recalibrating if a PT3 player is active in the
same program (50Hz gives a full frame's cycle budget to spread across
instead of half a frame, which needs different offsets, not just "more of
the same").

## Design: why this is safe, researched not assumed

- **Fully self-contained — never falls through to the ROM dispatcher
  (`$EE22`)**, unlike the generic IRQ-hooking pattern found in Oric forum
  examples (redirect `$0245`/`$0246`, then `BIT`/`BVC`/`JMP` through to the
  ROM's own handler for unrecognised sources). `oric_crt.c`'s IRQ NOTE
  documents the stock ROM handler corrupts zero page/screen RAM in this
  bare-metal context; independently, OSDK's
  ["Overlay Memory"](https://osdk.org/index.php?page=articles&ref=ART14)
  article confirms "the standard Oric IRQ" depends on ROM code and warns
  it breaks if ROM banking changes ("you must implement your own IRQ
  handler... risking system crashes" otherwise). This handler is pure RAM
  code with no ROM calls, directly or transitively, so it stays safe
  regardless of overlay-RAM state (see [loci.md](loci.md)'s
  `enable_overlay_ram()`).
- **Coexists with the "SEI forever" convention**: `hrirq_init()` does not
  enable interrupts — only `hrirq_start()` does, so any program that never
  calls it is behaviourally unchanged (verified: `make test`/
  `make test-hires` both stay green whether or not this module is used).
  Once `hrirq_start()` **is** active, any code touching VIA Port A
  ([ijk.md](ijk.md), `enable_overlay_ram()`) must use the existing
  `PHP/SEI...PLP` convention (`oric_crt.c`'s CONVENTION comment) — today
  that's inert (IRQs are always off), but it becomes a **live hazard** the
  moment interrupts are enabled.
- The handler entry itself uses Oscar64's own `__hwinterrupt` qualifier
  (full CPU register save, exits via `RTI` — see `oscar64manual.md`
  "Interrupt handlers"), not hand-written assembly.

## Callback safety: `__interrupt` is required

`_hrirq_handler` calls the registered `RasterCallback` through a stored
function pointer. Oscar64 emits `warning 2005: Calling non interrupt safe
function` at that call site **regardless of the target's qualifier** —
the compiler can't statically verify safety through an indirect call, only
through a direct call to a named function. This is **expected**, not a
bug: the burden is on the caller to declare `cb` as `__interrupt` (saves
Oscar64's ZP pseudo-register file, distinct from the raw CPU registers
`__hwinterrupt` already saves) — otherwise a callback that happens to
interrupt the main program mid-expression could corrupt whatever ZP state
the main program was using.

```c
__interrupt void my_split(void)
{
    hires_row_colors(100, A_FWWHITE, A_BGRED);
}

hrirq_init();
hrirq_add(2000, my_split);
hrirq_start();
// ... main loop / effect logic ...
hrirq_stop();
```

## Cycle budget and calibration

[OSDK's "Performance Profiling"](https://osdk.org/index.php?page=articles&ref=ART11)
gives concrete, sourced frame-timing constants for 1MHz/50Hz Oric:
**19,968 cycles/frame, 64 cycles/scanline**.

`cycle_offset` is **not cycle-exact by itself** — it drives a plain
busy-wait loop (a `for` loop over a volatile-memory-write side effect;
Oscar64's optimizer will eliminate a busy-wait that has no genuinely
observable effect, even one using a `volatile`-qualified *loop variable*
alone — confirmed the hard way when this module's own test was built, see
`tests/scripts/test_hires.sh`'s history). The real per-iteration cost of
that loop depends on Oscar64's generated code, which you can inspect
directly in the `.asm` build output (`build/*.asm`, generated whenever you
build with debug info) for your own specific loop.

Real raster-line-accurate effects need empirical calibration before
trusting a specific offset value:

- **Oricutron's built-in cycle counter** (per ART11): F2 for the debugger,
  F9 to zero the counter, CPU breakpoints with `z`/`c` flags to automate
  counting between two points in your code.
- **Phosphoric's cycle-accurate headless capture**
  (`--dump-ram-at`/`--screenshot-at`), the same technique
  `tests/scripts/test_boot.sh`'s `SPLASH_CYCLES` calibration already uses
  — dump a screenshot at varying cycle counts to see where a colour change
  visually lands.

## Verification

`tests/scripts/test_hires.sh` (via `make test-hires`) checks two things:
(a) a marker byte stays untouched when `hrirq_start()` is never called
(regression: `hrirq_init()`/`hrirq_add()` alone are inert), and (b) the
same marker byte gets set by a registered `__interrupt` callback after
`hrirq_start()`, proving the handler genuinely installs, Timer 1 actually
fires it, and it correctly dispatches to user code — not just that the
code compiles.
