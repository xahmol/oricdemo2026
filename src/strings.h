// strings.h - Localisation gateway
// Selects the correct language-specific string file at compile time.
// Usage: #include "strings.h" in any source that displays MSG_* strings.
// make LANG=FR (-> -dLANG_FR) selects strings_fr.h; default is strings_en.h.

#ifndef STRINGS_H
#define STRINGS_H

#ifdef LANG_FR
#include "strings_fr.h"
#else
#include "strings_en.h"
#endif

#endif // STRINGS_H
