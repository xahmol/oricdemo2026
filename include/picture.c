// picture.c - see picture.h.

#include "picture.h"
#include "arkos.h"
#include "rasterirq.h"
#ifdef STORAGE_FLOPPY
#include "floppy.h"
#else
#include "loci.h"
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
bool picture_load(const char *path, void *dst, uint16_t max_size)
{
    int16_t r;
    hrirq_stop();
    arkos_pause();
    r = file_load(path, dst, max_size);
    arkos_resume();
    hrirq_start();
    return r >= 0;
}
#endif
