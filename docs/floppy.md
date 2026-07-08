# Floppy-disk build target (tools/oric_floppybuilder.py, include/floppy.h)

A second, independent distribution target alongside the existing tape/LOCI
pipeline (`build/oricdemo.tap`): a bootable Microdisc floppy image
(`build/oricdemo_floppy.dsk`) that boots via the Microdisc disk controller's
own boot EPROM — no DOS/SEDORIC ever resident, full RAM mapped at
`$C000-$FFFF` for the whole session, and it works in plain Oricutron (unlike
the LOCI target, which Oricutron can't run at all since LOCI isn't emulated
there).

Built from two pieces: `tools/oric_floppybuilder.py`, a Python
reimplementation of OSDK's FloppyBuilder tool that assembles the `.dsk`
image from a script, and `tools/floppy/` (an adapted boot sector + resident
loader) plus `include/floppy.c/h` (the demo-facing runtime API), which give
running code LOCI-independent multi-file loading.

## Attribution

Adapted from OSDK (Oric Software Development Kit),
[github.com/Oric-Software-Development-Kit/osdk](https://github.com/Oric-Software-Development-Kit/osdk):

- `osdk/main/FloppyBuilder` (`FloppyBuilder.cpp`, `Floppy.cpp`,
  `lz_pack.cpp`) — the script DSL and `.dsk` byte format, traced precisely
  (see "What's precisely traced vs. adapted/simplified" below) into
  `tools/oric_floppybuilder.py`.
- `osdk/main/Osdk/_final_/sample/floppybuilder/code/loader.asm`,
  `loader_api.h`/`.s`, `sector_1-jasmin.asm`, `sector_2-microdisc.asm`,
  `sector_3.asm` — the reference boot-sector/resident-loader mechanics,
  adapted (not transliterated) into `tools/floppy/bootsector_microdisc.c`
  and `tools/floppy/loader.c`.

Per OSDK's own stated terms
([osdk.org/index.php?page=main](https://osdk.org/index.php?page=main)): free
to use, modify, fork, and extend the SDK or any part of it for anything,
including commercial products; not permitted to resell the SDK's own source
code or claim ownership of its files; attribution is "appreciated," not
mandatory (given here regardless, matching this project's own code-
attribution convention — see the root `CLAUDE.md`).

A real-world FloppyBuilder user
([github.com/Dhebug/Nova2026](https://github.com/Dhebug/Nova2026)) was
checked for a possibly more clearly-licensed loader — its own loader turned
out to be an unlicensed derivative of a *third* codebase ("Encounter" by
Chema/Fabrice), a dead end not used here.

## Two build targets, not one

| | Tape/LOCI (`all`/`run`/`usb`) | Floppy (`disk`/`run-disk`/`test-disk`) |
|---|---|---|
| Runtime | `include/oric_crt.c` | `include/oric_crt_floppy.c` |
| Entry point | `src/main.c` | `src/floppy_test.c` |
| Storage backend | `include/loci.c` (needs a LOCI device) | `include/floppy.c` (needs nothing but the disk itself) |
| Runs in plain Oricutron | No (LOCI unemulated) | Yes |
| File addressing | Runtime path string | Compile-time integer index |
| `pt3_load()` signature | `bool pt3_load(const char *path)` | `bool pt3_load(uint8_t file_index)` (`STORAGE_FLOPPY`) |

Both targets are built independently; nothing about the floppy target
changes the tape/LOCI pipeline, and vice versa (same "two separate builds
for two runtimes" pattern already established by `oric_crt.c` vs.
`oric_crt_hires.c` for HIRES mode).

## `tools/oric_floppybuilder.py` — script DSL

Invocation matches the original FloppyBuilder exactly:

```
oric_floppybuilder.py <init|build|extract> <script.txt> [-Dname=value ...]
```

`init` and `build` differ only in whether missing referenced files are
tolerated (placeholder content, used for the first pass of the two-pass
build below) or fatal. `extract` parses the same script but pulls files back
*out* of a built disk via `SaveFile`, skipping the final disk/header write —
useful for debugging a built `.dsk`, not on the critical build path.

Script commands:

| Command | Effect |
|---|---|
| `FormatVersion` | Declares the script format version (parsed, not otherwise checked). |
| `DefineDisk sides tracks sectors [interleave]` | `sides` must be 2; `interleave` defaults to 1 and must satisfy `1<=n<sectors`. |
| `OutputLayoutFile file` | Where to write the generated C header (see below). |
| `OutputFloppyFile file` | Where to write the final `.dsk`. |
| `SetPosition track sector` | Moves the write cursor. |
| `WriteSector file` | Writes one raw 256-byte sector (zero-padded, CRC recomputed), no file-table entry, auto-advances position. |
| `WriteLoader file address` | Writes the resident loader (only one allowed per disk, spans as many sectors as needed, never compressed). |
| `AddFile file` / `AddTapFile file` | Registers a file in the directory table (the latter strips a tape header first). |
| `AddDefine Name Value` | Macro-substitutes against the last `AddFile`d entry: `{FileIndex}`, `{FileSize}`, `{FileTrack}`, `{FileSector}`, `{FileDiskOffset}`. |
| `ReserveSectors N [Fill]` | Reserves N sectors, optionally filled with a byte value. |
| `SetCompressionMode [None\|FilePack]` | See scope cuts below — only `None` is implemented. |
| `SaveFile` | Extract-mode only. |

This project's own script: `tools/floppy/disk_script.txt`.

### `.dsk` byte format (traced precisely from the C++ source)

256-byte header: `"MFM_DISK"` (8 bytes, no null terminator), then
little-endian 32-bit `sides`/`tracks`/`geometry` (`geometry` is always 1),
zero-padded to 256 bytes total. Body: side-major then track-major (every
track of side 0, then every track of side 1). Each track is exactly 6400
bytes; sectors are 256 bytes each.

Per track: `gap1` bytes of `0x4E`, then per physical sector slot (placed via
a round-robin interleave algorithm, not logical sector order): 12×`0x00`,
3×`0xA1` + `0xFE` (ID address mark), track/side/sector/size-code bytes plus
a CRC-16 over those 4 bytes, `gap2` bytes of `0x4E`, 12×`0x00`, 3×`0xA1` +
`0xFB` (data address mark), 256 data bytes (zero-filled at format time) plus
a CRC-16 over the 4 marker bytes + 256 data bytes, `gap3` bytes of `0x4E`;
any track remainder is padded with `0x4E`.

Gap table (sectors/track → gap1/gap2/gap3): 15/16/17 → 72/34/50; 18 →
40/34/34; any other sector count is a hard error.

CRC-16: CCITT variant, polynomial `0x1021`, initial value `0xFFFF`.

### Generated header (`OutputLayoutFile`)

Plain C, not the reference's dual `#ifdef ASSEMBLER`/`#ifdef LOADER` xa65-
compatibility trick (nothing here ever needs the assembler-syntax half,
since the loader is Oscar64 C, not xa65):

```c
// build/floppy_directory.h -- generated by tools/oric_floppybuilder.py, do not edit.
#define FLOPPY_SIDE_NUMBER        2
#define FLOPPY_TRACK_NUMBER       80
#define FLOPPY_SECTOR_PER_TRACK   17
#define FLOPPY_LOADER_ADDRESS     64000
#define FLOPPY_FILE_COUNT 3
#define LOADER_DEMO_FILE     0
#define LOADER_PAYLOAD_FILE  1
#define LOADER_MUSIC_FILE    2
static const uint8_t  FloppyFileStartSector[FLOPPY_FILE_COUNT] = { 10, 15, 16 };
static const uint8_t  FloppyFileStartTrack [FLOPPY_FILE_COUNT] = { 0, 1, 1 };
static const uint16_t FloppyFileSize       [FLOPPY_FILE_COUNT] = { 5432, 64, 600 };
```
(real output, from this project's own `tools/floppy/disk_script.txt` build)

## Boot sector + resident loader

### Scope: Microdisc only for v1 — Jasmin stays on the roadmap

Phosphoric (this project's only headless test path) only emulates
Microdisc, so Microdisc-only is the right v1 scope for what's actually
testable. **Jasmin support is deferred, not dropped**: real Oric hardware in
the wild includes systems with only a Jasmin controller, so full hardware
compatibility genuinely needs both eventually. Nothing in the v1 design
should make adding Jasmin harder later:

- The resident loader's FDC access uses named register/command constants
  (`FDC_STATUS_REGISTER`, `CMD_SEEK`, `CMD_READ_SECTOR`, etc.), not
  hardcoded literals scattered through the logic — this is what would let
  the reference's own "self-patch every FDC address for Jasmin at boot,
  based on which controller loaded us" trick be added later as a contained,
  additive change.
- `tools/oric_floppybuilder.py`'s script DSL and disk-format code are
  already controller-agnostic (FloppyBuilder itself supports both via
  different boot sectors, same tool).
- `tools/floppy/bootsector_microdisc.c` is named/scoped as *one of
  potentially two* boot-sector variants, not the only one — adding
  `bootsector_jasmin.c` alongside it, plus the loader's runtime
  controller-detection/address-patch logic, is a well-isolated follow-up.

### The "chain of overlay programs" vs. "one long-running demo" mismatch

The reference loader is designed for a *chain* of small programs, each
returning from `main()` to hand control back to a `forever_loop` that loads
and jumps to the *next* one. This project's `main()` never returns — every
runtime here (`oric_crt.c`, `oric_crt_hires.c`, `oric_crt_floppy.c`) spins
forever on return. So the reentrant chaining machinery
(`InitializeFileAt`/`forever_loop`) is dropped as unneeded dead code; only
two things are actually needed and implemented:

1. A **one-shot boot handoff**: load file index 0 (the demo binary) to its
   fixed load address and jump there, once, at the end of the loader's own
   init.
2. The reference's `LoadFileAt` equivalent (`LoadData`, reached via a fixed
   trampoline) — a synchronous "load now, return to caller" primitive,
   usable at any point for the rest of the program's life.

### `include/floppy.c/h` — the demo-facing API

```c
int16_t floppy_load(uint8_t file_index, void *dst, uint16_t max_size);
```

Loads `file_index` (see the generated `build/floppy_directory.h`'s
`LOADER_*_FILE` `#define`s) into `dst`, capped at `max_size`. Returns the
file's real size on success, or `-1` if it doesn't fit. Synchronous —
returns to the caller like `loci.c`'s `file_load()`, not like the
reference's `InitializeFileAt`/`forever_loop` chaining.

Fixed API cells (`$FFEF-$FFF5`) and the `jsr $FFF7` trampoline **must
exactly match** `tools/floppy/loader.c`'s own addresses — the two files are
compiled completely separately (the loader is its own standalone program,
embedded into the disk image by `tools/oric_floppybuilder.py`; `floppy.c` is
linked into the demo binary), so nothing enforces this agreement except both
files' own comments.

### `pt3_load()`'s split signature — real and intentional

```c
#ifdef STORAGE_FLOPPY
bool pt3_load(uint8_t file_index);   // floppy target
#else
bool pt3_load(const char *path);     // tape/LOCI target
#endif
```

Floppy files are addressed by a compile-time integer index (there's no
runtime directory to search — only a fixed file table baked into the disk
image at build time), not a runtime path string. Unifying the signature
across targets would be dishonest about this real difference. Call sites:
`pt3_load(LOADER_MUSIC_FILE)` on floppy vs. `pt3_load("music.pt3")` on
tape/LOCI.

### The IRQ-vector bridge (why `rasterirq.c`/`pt3.c` work unmodified here)

`rasterirq.c` installs its handler at `$0245`/`$0246` (a RAM cell the *ROM*
conventionally jumps through), not the real 6502 IRQ vector. With ROM
permanently gone on the floppy target, `$FFFE`/`$FFFF` are real RAM — and
exactly where the resident loader's own code lives. Fix: the loader's
one-shot init sets `$0245`/`$0246` to a safe RTI stub and `$FFFE`/`$FFFF` to
`JMP ($0245)` — an indirect jump through the same low-RAM cell `rasterirq.c`
already expects. This makes `hrirq_init()`/`pt3_tick()` work **unmodified**
on the floppy target.

### Implementation approach: Oscar64-compiled, not hand-assembled xa65

Both the boot sector (`tools/floppy/bootsector_microdisc.c`) and the
resident loader (`tools/floppy/loader.c`) are small standalone Oscar64
programs (`-tf=bin`, no `-rt=` override, minimal `#pragma region`
placement) — not `include/`-library files (never `#include`d by
application code; separate build artifacts embedded into the `.dsk` by
`tools/oric_floppybuilder.py`). This matches the project's existing
"everything Oscar64, minimal external tooling" approach and avoids
introducing `xa65` as a new toolchain dependency.

The Microdisc boot sector's self-relocation trick (reading its own return
address off the 6502 stack via a patched RTS + `jsr $0000`, to discover its
own load address, then copying itself to `$9800`) was confirmed working in
Oscar64 via a spike before the rest of the loader was built on this
approach. One hard lesson from that spike: `(ptr),Y` indirect addressing
**requires `__zeropage` pointer variables** — Oscar64 silently misassembles
otherwise (no compile error, just wrong runtime behavior at execution time).

Two named `__asm` block limitations discovered along the way, both
documented in `bootsector_microdisc.c`'s own comments:

- No label arithmetic inside a named `__asm` block — byte offsets/counts
  (e.g. the relocation offset, the payload byte count) must be hand-computed
  constants, and re-computed if the surrounding code changes.
- No raw byte-literal directive — a fixed hardware-protocol header (the
  Microdisc EPROM's 23-byte sanity-check header) can't be emitted from C
  source at all, and is instead prepended externally by
  `tools/floppy/extract_bootsector.py`, which also extracts just the
  `bootsector` labeled block's bytes out of the compiled `.bin`/`.map` (the
  default runtime wraps it in CRT/startup scaffolding this sector doesn't
  need).

## Two-pass build (see the `Makefile`'s floppy section)

`include/floppy.c` needs `build/floppy_directory.h`, which needs the
compiled *size* of the demo binary itself (FloppyBuilder places files
sequentially, so a later file's start sector depends on an earlier file's
size) — a circular dependency, resolved via two compile passes:

1. Compile the loader once with placeholder `DEMO_TRACK`/`DEMO_SECTOR`/
   `DEMO_SIZE` values, and compile the demo once with a checked-in
   placeholder `floppy_directory.h` (same array shapes, dummy values) —
   purely to learn the demo's real compiled size.
2. Run `tools/oric_floppybuilder.py init` to generate the real header from
   that size.
3. Recompile the demo (same size — only the header *values* changed, not
   array lengths) and the loader (with the real `DEMO_TRACK`/`DEMO_SECTOR`/
   `DEMO_SIZE`, and `DEMO_ADDRESS` — see "Known issues" below for why this
   last one matters) to get the final binaries.
4. Run `tools/oric_floppybuilder.py build` to produce the final `.dsk`,
   embedding the final demo binary, boot sector, and loader.

Makefile targets: `disk` (builds `build/oricdemo_floppy.dsk`), `run-disk`
(launches Oricutron with `--disk-rom microdisc.rom -d ...`), `test-disk`
(headless Phosphoric verification, see below).

## Testing

`tests/scripts/test_disk.sh` (`make test-disk`), mirroring `test_boot.sh`'s
structure: boots `build/oricdemo_floppy.dsk` under Phosphoric's Microdisc
emulation (`--disk-rom`, no LOCI/tape at all) and asserts `src/floppy_test.c`'s
status lines render, including a `floppy_load()` payload check and a
`pt3_load(file_index)` + one-tick AY-register-shadow assertion (same spirit
as `test_boot.sh`'s own `music.pt3` check).

**What Phosphoric genuinely cannot verify**: Jasmin (not emulated at all —
moot, v1 is Microdisc-only); the self-relocating boot sector's behavior
against real EPROM timing (Phosphoric models the register-level contract,
not cycle-exact firmware); real floppy-drive mechanics.

**Needs real Oricutron or hardware**: Jasmin once added; audible PT3
playback confirmation (RAM-dump assertions prove decode correctness, not how
it sounds — same caveat as the tape target, see `docs/pt3.md`); an actual
"does it boot on a real Atmos + Microdisc" hardware test.

## What's precisely traced vs. adapted/simplified

- **Precisely traced**: the `.dsk` byte format (header, gaps, CRC-16,
  interleave placement) and the script DSL's command set/macro
  substitution.
- **Deliberately simplified/scoped out**:
  - `SetCompressionMode FilePack` is a hard error, not silently accepted —
    only `None` is implemented. FilePack is a specific LZ77 variant (12-bit
    offset/4-bit length match tokens), a genuinely separate algorithm to
    port correctly, and isn't needed here (one program plus a handful of
    uncompressed assets fit comfortably on a 2-sides×80-tracks×17-sectors
    disk).
  - `LoadDiskTemplate` is unimplemented (fresh-disk builds only) — this
    lets the per-sector byte-offset table be computed directly from the
    gap-table formula instead of scanning freshly-formatted MFM bytes for
    address marks (the reference's actual approach, needed there
    specifically for foreign/templated disks). If `LoadDiskTemplate` is
    ever added, the offset table must switch to scan-based lookup.
  - `SetPosition`'s bound check replicates the reference's *actual*
    behavior (track range 0-41 in the original, scaled here), not its own
    comment's claim — noted so a future reader doesn't "fix" it into a
    regression.
  - The reentrant `InitializeFileAt`/`forever_loop` program-chaining
    machinery is dropped entirely (see "chain of overlay programs" above).
  - Jasmin disk-controller support (deferred, not dropped — see "Scope"
    above).

## Known issues

Both found and fixed during development (see git history for the exact
commits), documented here since they were genuinely non-obvious and are
worth knowing if this code is touched again:

- **Track 0 sectors 1 and 3 both need specific, non-obvious content, or
  boot fails entirely.** Empirically discovered (not documented anywhere
  obvious in the reference): the Microdisc EPROM's own directory sanity
  check requires a fixed, non-executable "fake directory entry" blob at
  track 0 sector 3 (`tools/floppy/directory_sanity_sector.bin`, traced from
  OSDK's `sector_3.asm`) *and* a specific 64-byte header containing a
  literal `"SEDORIC "` filesystem-name signature at track 0 sector 1
  (`tools/floppy/sector1_header.bin`, traced from `sector_1-jasmin.asm`) —
  even though this project doesn't implement Jasmin at all, and sector 1 is
  nominally "Jasmin's own boot-sector slot," never executed by the
  Microdisc EPROM. Confirmed via a three-way experiment: sector 1 left
  blank → "No operating system on disc"; filled with an arbitrary non-zero
  placeholder → "insert system disc" (a different failure, proving content
  genuinely matters); the real reference header bytes → boot proceeds past
  this check.
- **`LoadData`'s fetch loop used to always store a full 256-byte sector,
  regardless of how many bytes were actually requested.** The WD1793/
  Microdisc controller always *streams* a full 256-byte sector once a read
  is triggered (a sector read can't be partially aborted), which is
  correct and necessary — but the bug was storing every one of those 256
  bytes to the destination buffer unconditionally, even for the final,
  partial sector of a request smaller than a sector multiple. Confirmed via
  a direct test: a 64-byte `floppy_load()` request correctly delivered its
  first 64 bytes, but also overwrote the following 192 bytes of caller
  memory with the sector's own zero-padding — a real, silent buffer
  overflow, not a documented tradeoff (an earlier version of this file's
  own code comment incorrectly described it as intentional). Fixed by
  tracking whether the current sector is the final one (`l_bytes_hi == 0`)
  and, if so, only storing up to the actual remaining byte count while
  still reading (and discarding) the rest of the sector to keep the FDC
  transfer protocol correctly synced. See `tools/floppy/loader.c`'s
  `fetch_byte`/`seek_done` comments.
- **Still open, narrow, and not blocking core functionality**: the AY
  mixer register (byte 7 of `pt3_tick()`'s computed output) reads `0x3F`
  on this target for the same `tests/fixtures/music.pt3` fixture that
  produces `0x3C` on the tape/LOCI target — a real, confirmed discrepancy
  (bits 0/1, channels A/B, computed as disabled here). Narrowed down but
  not root-caused: the module data loads byte-for-byte identically on both
  targets (checked directly against the fixture), and the persistent
  per-channel state (`pt3_chan[].enabled`/`.vibrato_audible`) reads
  identically on both targets too — so the divergence is somewhere in
  `pt3_tick()`'s own locally-computed mixer byte specifically on this
  target, not in loading or in persistent channel state. Every other AY
  register (tone, amplitude, noise period) matches exactly.
  `tests/scripts/test_disk.sh` asserts the actually-observed `0x3F` value
  (not the tape/LOCI target's `0x3C`), so the test reflects real, verified
  behavior rather than an unverified assumption.
- **`tools/floppy/loader.c`'s boot handoff must jump to the runtime's
  `startup` region (`$0500`), not straight into `main()` (`$0580`).** The
  Makefile must pass `-dDEMO_ADDRESS=0x0500` to every `loader.c` compile —
  the loader's own default (`0x0580`, chosen to match the "no explicit
  override" case) skips `oric_crt_floppy.c`'s `SEI`/BSS-clear/zero-page-
  clear/stack-setup entirely if left unset. Confirmed empirically as the
  root cause of a full early hang: with the wrong entry point, an interrupt
  fired before the demo's own `SEI` could execute and vectored through
  uninitialized zero-page garbage. See `Makefile`'s
  `FLOPPY_DEMO_ENTRY_ADDRESS` comment.

## What this doesn't cover

- Jasmin disk-controller support (deferred, not dropped — see "Scope"
  above).
- FilePack/LZ77 compression (hard error if requested).
- `LoadDiskTemplate` (fresh-disk builds only).
- Automatic reclamation of the full freed `$C000-$FFFF` region for the
  general Oscar64 allocator — `oric_crt_floppy.c` keeps the same
  `$0580-$B1FF` main region as `oric_crt.c` for v1; the extra headroom this
  target's permanently-gone ROM could in principle offer is available for
  explicit, opt-in use (e.g. a large named buffer) but not folded into the
  general allocator automatically. A worthwhile follow-up, not a blocker.
- Real hardware validation (a physical floppy write/boot test).
