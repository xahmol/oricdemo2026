# Runtime picture loading (picture.h)

Loads a pre-rendered HIRES bitmap (`tools/oric_pictconv.py`'s raw
`--format bin` output — an 8000-byte `HIRESVRAM`-sized image, or fewer
bytes for a partial/progressive load) from disk at runtime, instead of
compiling it into a C array. This keeps every picture asset entirely out
of the ~36.1 KB main code/data/BSS budget (`docs/hires.md`) — the same
reasoning that already keeps the Arkos music module out of it (see
`docs/arkos.md`). Include `picture.h`; it auto-compiles `picture.c` via
`#pragma compile`.

```c
#ifdef STORAGE_FLOPPY
bool picture_load(uint8_t file_index, void *dst, uint16_t max_size);
#else
bool picture_load(const char *path, void *dst, uint16_t max_size);
#endif
```

Same dual-signature convention as `arkos_load()` (`docs/arkos.md`) — not
a bug, a real, intentional difference between the two targets:

- **Tape/LOCI target**: `picture_load(const char *path, ...)`, via
  `include/loci.h`'s `file_load()`. Every picture must ship alongside the
  program's own `.tap` file in the same LOCI folder (see the project
  `README.md`'s Installation section).
- **Floppy target** (`-dSTORAGE_FLOPPY`): `picture_load(uint8_t
  file_index, ...)`, via `include/floppy.h`'s `floppy_load()`. Files are
  addressed by a compile-time integer index baked into the disk image at
  build time (`tools/floppy/disk_script_demo.txt`), not a path string.

`dst` is almost always `(void *)HIRESVRAM` directly — `picture_load()`
writes straight into the live screen, no separate staging buffer or blit
step. `max_size` caps how many bytes are written; a call with a SMALLER
`max_size` than the file's own length only overwrites that many leading
bytes of `dst`, leaving whatever was already there in the untouched tail
— this is deliberately exploitable for a progressive/partial reveal (call
again later with a larger `max_size` to fill in more), though no section
in this demo currently does that (an earlier one,
`src/section_dissolve_showcase.c`, did — see `docs/dissolve.md`'s own
note on why that section was later removed). Returns `true` on success;
silently returns `false` on failure (no LOCI/floppy device, file not
found) — the same graceful-degradation posture as `arkos_load()`, so
callers don't need to gate the whole program on a picture load
succeeding.

**Every call brackets the underlying load with `hrirq_stop()` +
`arkos_pause()` before, `arkos_resume()` + `hrirq_start()` after** —
neither `file_load()` nor `floppy_load()` is safe to call while
`arkos_tick()` is ticking live: on the tape/LOCI target, `file_load()`
would be writing into the exact `$C000` buffer `arkos_tick()`
concurrently decodes; on the floppy target, the resident loader's own
sector-read path has no interrupt protection at all. `arkos_pause()`/
`arkos_resume()` (not `arkos_stop()`/`arkos_init()`) mean the CURRENT
track's held note resumes exactly where it left off, at its exact
pre-load volume, once the picture has loaded — a brief pause, not a
restart. See `docs/arkos.md`'s "Pause vs. stop" section for the full
mechanism.
