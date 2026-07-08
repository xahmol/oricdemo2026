# Build notes

### Compiler

Oscar64 (`$OSCAR64_HOME/bin/oscar64`, default `~/oscar64/`). Standard flags
(from `Makefile`):

```
-n -tf=bin -rt=include/oric_crt.c -i=include -i=src -O2 -dNOFLOAT \
  -dVERSION_MAJOR=$(VERSION_MAJOR) -dVERSION_MINOR=$(VERSION_MINOR) \
  -dVERSION_PATCH=$(VERSION_PATCH)
```

- `-i=include -i=src`: lets `#pragma compile` chains resolve both the
  generic library `.c` files in `include/` and this project's own `.c`
  files in `src/`.
- `-dVERSION_MAJOR/MINOR/PATCH`: available to any source file that wants to
  report a build version (not currently displayed anywhere in `src/main.c`).
- `-dNOFLOAT`: disables `printf`/`scanf`'s `%f` format support only — float
  *arithmetic* still works fine (see `include/crt_math.c`); this is what
  makes [vector3d.md](vector3d.md) copyable as-is.
- HIRES-mode builds (`src/hires_test.c`, or any future program using
  [hires.md](hires.md)) use a **different** runtime,
  `-rt=include/oric_crt_hires.c` instead of `-rt=include/oric_crt.c` — see
  `CFLAGS_HIRES`/`MAIN_HIRES_SRCS` in the `Makefile`, and
  `include/oric_crt_hires.c`'s header comment for why (HIRES mode needs
  `$9800-$BFDF`, which the default runtime's region layout uses for
  code/data/stack).
- The floppy-disk target ([floppy.md](floppy.md)) uses a **third** runtime,
  `-rt=include/oric_crt_floppy.c`, entered via `tools/floppy/loader.c`'s
  boot handoff rather than a tape auto-run. Its Makefile build is a
  **two-pass** sequence, not a single compile: `include/floppy.c` needs
  `build/floppy_directory.h`, which needs the compiled *size* of the demo
  binary itself (a circular dependency) — resolved by compiling once with a
  checked-in placeholder header purely to learn the real size, running
  `tools/oric_floppybuilder.py init` to generate the real header from that
  size, then recompiling (same size, only header *values* change) for the
  final binary and final `.dsk`. See [floppy.md](floppy.md)'s "Two-pass
  build" section for the full sequence and the `Makefile`'s floppy section
  for the actual targets.

### #pragma compile chain

Each library header ends with `#pragma compile("filename.c")`. Oscar64
resolves this path relative to the include search directories (`-i=include
-i=src`, in that order).

- **Library `.c` files** (generic, reusable across projects) live in
  `include/`: `oric_crt.c` (default runtime), `oric_crt_hires.c` (HIRES-mode
  runtime), `crt_math.c`, `charwin.c`, `keyboard.c`, `charset.c`, `ijk.c`,
  `loci.c`, `hires.c` (see [hires.md](hires.md)), `vector3d.c` (see
  [vector3d.md](vector3d.md)).
- **App-specific `.c` files** live in `src/`: `main.c`.

### Known Oscar64 gotchas

| Symptom | Cause | Fix |
|---|---|---|
| `va_arg` crash | `va_arg` is broken in native mode (`-n`) | Do not use `<stdarg.h>` |
| `#if MACRO` fails when set via `-d` | `-d` defines have no value | Use `#ifdef MACRO` |
| Cast applied before member access | Oscar64 parses `(type)struct.member` wrong | Use a temp variable |
| Ternary `? ptr : 0` in pointer function | Parser issue | Use `if`/`return` |
| Large local array crashes | 6502 stack is only 256 bytes | Declare large arrays `static` |
| `snprintf` not found | Not in Oscar64 `<stdio.h>` | Use `sprintf` instead |
| `-O2` caller-save set under-counted, stray screen garbage on a specific call site | Whole-program register allocator miscounts live registers across a call | Force a larger save set with a dummy `sprintf` to unused scratch RAM — see `oscar64manual.md` "`-O2` whole-program register allocator: caller-save set can be under-counted". **In this project, `$A000` is NOT safe scratch RAM for that trick** (unlike a TEXT-mode-only project) — it's the live HIRES bitmap base (see [hires.md](hires.md)); pick an address outside `$A000-$BFDF` instead, and outside `$9800-$9FFF` if the program also uses `oric_crt_hires.c`. |
| `(ptr),Y` indirect addressing silently misassembles (no compile error, just wrong runtime behavior) | Pointer variable isn't `__zeropage` | Declare the pointer `static __zeropage` — see [floppy.md](floppy.md)'s boot-sector self-relocation notes |
| Named `__asm` block can't do label arithmetic or emit raw byte literals | Both are genuine limitations of Oscar64's named-asm-block syntax, not oversights to work around in-place | Hand-compute and hardcode byte offsets/counts (re-derive if the surrounding code changes); emit fixed hardware-protocol byte blobs from an external post-processing script instead (see `tools/floppy/extract_bootsector.py`) |
| A program jumped into directly (not via tape auto-run) silently runs with interrupts enabled, uninitialized BSS/zero-page, and a garbage software stack pointer | The jump landed on the runtime's `main` region (e.g. `$0580`) instead of its `startup` region (e.g. `$0500`), skipping the runtime's own `SEI`/BSS-clear/ZP-clear/stack-setup entirely | Always enter a custom runtime at its `startup` region's address, never `main`'s — see [floppy.md](floppy.md)'s "Known issues" (`tools/floppy/loader.c`'s `DEMO_ADDRESS`) for a full empirical writeup of this exact failure |
