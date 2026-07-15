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
// same reason, as arkos.c's arkos_load()). static, not a local/stack
// buffer -- this project's own "large arrays must be static" discipline.
static char picture_load_path[HOMEDIR_MAXLEN + 32];

bool picture_load(const char *path, void *dst, uint16_t max_size)
{
    int16_t r;
    hrirq_stop();
    arkos_pause();
    homedir_join(picture_load_path, path);
    r = file_load(picture_load_path, dst, max_size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#endif
