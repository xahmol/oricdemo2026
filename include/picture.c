// picture.c - see picture.h.

#include "picture.h"
#include "arkos.h"
#include "rasterirq.h"
#ifdef STORAGE_FLOPPY
#include "floppy.h"
#else
#include "loci.h"
#include "homedir.h"
#endif

#ifdef STORAGE_FLOPPY
bool picture_load(uint8_t file_index, void *dst, uint16_t max_size)
{
    int16_t r;
    hrirq_stop();
    arkos_pause();
    r = floppy_load(file_index, dst, max_size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#else
// Real-hardware fix: see homedir.h's own header comment (same fix,
// same reason, as arkos.c's arkos_load()). Reuses arkos_load()'s own
// scratch buffer (arkos_load_path_buf(), see arkos.h's own comment on
// that declaration) instead of a private HOMEDIR_MAXLEN-sized static
// buffer here -- the main code/data/BSS budget (~36.1KB, docs/hires.md)
// is tight enough that a second copy of that ~96-byte buffer doesn't
// fit once voice.c's own two call sites are compiled in too (a real,
// confirmed link-time BSS overflow otherwise). Safe: none of
// arkos_load()/picture_load()/voice_load() ever run concurrently with
// each other, and each freshly repopulates the buffer via
// homedir_join() before using it.
bool picture_load(const char *path, void *dst, uint16_t max_size)
{
    int16_t r;
    char *path_buf = arkos_load_path_buf();
    hrirq_stop();
    arkos_pause();
    homedir_join(path_buf, path);
    r = file_load(path_buf, dst, max_size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#endif
