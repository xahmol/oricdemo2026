// picture.h - runtime loader for pre-rendered HIRES bitmap assets
//
// Mirrors arkos.h's own #ifdef STORAGE_FLOPPY dual-signature dispatch,
// nothing else: pre-rendered pictures (oric_pictconv.py output, `--format
// bin`) load from disk/LOCI at runtime instead of being compiled into a
// C array, keeping them out of the ~36KB main code/data/BSS budget
// entirely -- the same reasoning that already keeps the Arkos music
// module out of it (see docs/arkos.md).
//
// Every call brackets the underlying file_load()/floppy_load() with
// hrirq_stop()+arkos_pause() before and arkos_resume()+hrirq_start()
// after: neither loading path is safe to call while arkos_tick() is
// ticking live (see docs/arkos.md's "Pause vs. stop" section) --
// file_load() would be writing into the exact $C000 buffer arkos_tick()
// concurrently decodes, and floppy_load()'s own sector-read timing budget
// has no interrupt protection at all. arkos_pause()/arkos_resume() (not
// arkos_stop()/arkos_init()) mean the CURRENT track's held note resumes
// exactly where it left off, at its exact pre-load volume, once the
// picture has loaded -- not a restart.

#ifndef PICTURE_H
#define PICTURE_H

#include <stdint.h>
#include <stdbool.h>

// Loads a pre-rendered HIRES picture (oric_pictconv.py's raw --format bin
// output, e.g. a full 8000-byte HIRESVRAM-sized image) into dst, capped
// at max_size. Returns true on success. Same dual-signature convention as
// arkos_load() -- see arkos.h's own comment for why this isn't a bug:
//   - Tape/LOCI target: picture_load(const char *path, ...), via
//     include/loci.h's file_load().
//   - Floppy target (-dSTORAGE_FLOPPY): picture_load(uint8_t file_index, ...),
//     via include/floppy.h's floppy_load().
#ifdef STORAGE_FLOPPY
bool picture_load(uint8_t file_index, void *dst, uint16_t max_size);
#else
bool picture_load(const char *path, void *dst, uint16_t max_size);
#endif

#pragma compile("picture.c")

#endif // PICTURE_H
