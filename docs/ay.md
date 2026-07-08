# AY-3-8912 register-write helper (ay.h)

A single, correct implementation of the Oric's AY-3-8912 sound chip write
protocol — used by [pt3.h](pt3.md), and the intended replacement for any
future code that would otherwise hand-roll this sequence itself. Include
`ay.h`; it auto-compiles `ay.c` via `#pragma compile`.

```c
void ay_write(uint8_t reg, uint8_t value);
```

Selects AY register `reg` and writes `value` to it. Safe to call from
normal code or from a `pt3_tick()`-style `__interrupt` context.

## The protocol, and a real correction this project shipped with

The Oric doesn't expose the AY chip's BC1/BDIR bus-control lines on VIA
Port B bits 6-7, despite an **earlier version of `include/oric.h`'s own
comment claiming exactly that** — a genuine documentation bug in this
project, now fixed. The real protocol drives BC1/BDIR through **VIA PCR**
(`$030C`), confirmed correct by two independent, mutually-agreeing sources:
this project's own already-working `include/keyboard.c` (which selects AY
register 14 for keyboard column drive using exactly this sequence), and
6502Nerd/dflat's PT3 player (`ppt3.s`'s `ROUT` routine, see [pt3.md](pt3.md)
for the full attribution).

Write sequence for AY register `N` = value `V` (`VIA_ORA` = `VIA.pra2` =
`$030F`, the no-handshake Port A; `VIA_PCR` = `VIA.pcr` = `$030C`):

```
VIA_ORA = N;  VIA_PCR = 0xFF (select+set-address);  VIA_PCR = 0xDD (deselect);
VIA_ORA = V;  VIA_PCR = 0xFD (select+write-data);    VIA_PCR = 0xDD (deselect);
```

`ay_write()` brackets this whole sequence in `PHP`/`SEI`/`PLP` (matching
`include/ijk.c`'s existing convention for VIA Port A hazards — PHP/PLP
preserves whatever the interrupt-enable state already was, rather than
assuming it, since `ay_write()` must be safe to call both from normal code
and from inside an already-interrupted context). **`include/keyboard.c`'s
`keyb_scan()` needed the same fix** — it did raw VIA/PCR AY access with no
protection at all, harmless only because this project never enabled
interrupts before [rasterirq.h](rasterirq.md) existed. Any code that touches
the AY chip or VIA Port A from within an active `hrirq_start()` context
must respect this convention.

## Register map

See `include/oric.h`'s `AY_REG_*` constants (all 14 registers: tone A/B/C
period lo/hi, noise period, mixer, volume A/B/C, envelope period lo/hi,
envelope shape). `AY_REG_IOA`/`AY_REG_IOB` (14/15) are the keyboard-related
registers `keyboard.c` uses — not audio registers.

## Why Phosphoric can't verify this directly

Phosphoric's `--dump-ram-at` dumps RAM only, not memory-mapped I/O — reading
back `$030F`/`$030C` after a real `ay_write()` call returns flat `0x00`
regardless of what was written (confirmed empirically). `ay_write()` itself
is therefore only smoke-tested (compiles, links, doesn't crash) by this
project's own test suite; [pt3.md](pt3.md) verifies the *computation* that
feeds into `ay_write()` calls via a separate register-value shadow array,
which is a real, byte-exact, RAM-dumpable proxy for correctness.
