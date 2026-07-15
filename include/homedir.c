// homedir.c - see homedir.h.

#include <string.h>
#include <stdbool.h>
#include "loci.h"
#include "homedir.h"

static char homedir[HOMEDIR_MAXLEN + 1];
static bool homedir_captured;

void homedir_join(char *out, const char *name)
{
    uint8_t len;

    if (!homedir_captured)
    {
        loci_getcwd(homedir, sizeof(homedir));
        homedir_captured = true;
    }

    strcpy(out, homedir);
    len = (uint8_t)strlen(out);
    if (len > 0 && out[len - 1] != '/')
    {
        out[len] = '/';
        out[len + 1] = '\0';
    }
    strcat(out, name);
}
