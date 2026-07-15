// homedir.h - LOCI boot-directory-relative path helper
//
// Every LOCI file path this project's arkos.c/picture.c open (the two
// music tracks, every picture) used a bare relative filename (e.g.
// "steppingout.aky"), trusting LOCI's own "current directory" was
// already correct at the time of the call. This held under Phosphoric's
// own --loci-flash test mode (confirmed via this project's own test
// suite), but NOT on real LOCI hardware with more than one mounted
// storage device (a real user report: files present and correctly
// located in the launched .tap's own folder on a USB-mounted drive, but
// silently failing to load anyway) -- the exact same failure mode this
// project's own sibling OricScreenEditorLOCI hit and fixed on
// 2026-06-20 (see that project's src/homedir.c's own header comment for
// the full writeup), via the identical mechanism ported here: capture
// LOCI's own GETCWD result once (the directory the currently-running
// .tap was actually loaded from, however the user navigated to it) and
// prefix that onto every subsequent bare filename before opening it.
//
// Unlike OricScreenEditorLOCI's own homedir.c (which also tracks a
// separate, user-navigable app.filedir for its own interactive file
// picker), this project has no interactive file browser -- every asset
// path is a compile-time-known literal -- so this is deliberately just
// the boot-directory half of that design, with the capture made lazy
// (on first use, inside homedir_join() itself) rather than requiring an
// explicit homedir_init() call at a specific point in main(): this
// project's own first LOCI file access happens inside arkos_load(),
// called from main(), and there's no benefit to adding a second,
// separately-ordered call site just to match a convention this project
// doesn't otherwise need.

#ifndef HOMEDIR_H
#define HOMEDIR_H

#define HOMEDIR_MAXLEN 64

// Prefixes LOCI's own boot-time current working directory onto `name`,
// writing the joined path into `out`. Lazily captures the boot directory
// via loci_getcwd() on its own first call ever (safe to call from any
// LOCI file access site, in any order -- captured at most once for the
// program's whole runtime). If the capture comes back empty (no LOCI
// present, or LOCI_OP_GETCWD errors -- confirmed to happen under
// Phosphoric's --loci-flash test mode, which bypasses the mount system
// entirely), this is a harmless no-op prefix: out ends up identical to
// name, i.e. exactly this project's own pre-fix behaviour.
//
// out must be at least HOMEDIR_MAXLEN + strlen(name) + 2 bytes.
void homedir_join(char *out, const char *name);

#pragma compile("homedir.c")

#endif // HOMEDIR_H
