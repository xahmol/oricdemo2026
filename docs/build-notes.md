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
