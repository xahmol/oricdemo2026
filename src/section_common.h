// section_common.h - shared "this section is done" signal for main.c's
// run_section() and every src/section_*.c file.
//
// Every section's tick() is `void`, not `bool` -- a section that reaches a
// natural end (e.g. section_splash.c's fade-out completing) calls
// section_mark_finished() instead of returning a value. This is a
// deliberate, permanent design choice, not a style preference: an earlier
// design had tick() return bool ("finished") through a stored function
// pointer in main.c's DemoSection table, and hit a real Oscar64 -O2
// code-generation bug where a tick() implementation whose tail is [call a
// void helper][load/compute a value][RTS] with nothing else in between
// doesn't reliably get its return value stored anywhere durable -- see
// section_logo.c's own git history and ~/.claude/oscar64.md for the full
// writeup. A workaround forcing the store via inline asm was tried and
// confirmed UNSAFE in practice (crashed the floppy target to Oricutron's
// monitor, hung Phosphoric outright, most likely by clobbering zero-page
// state something else nearby still needed). Signalling "finished" via a
// plain function call instead sidesteps the whole bug class permanently:
// a void function's return path is never routed through the compiler's
// return-value machinery at all, so there is nothing for this bug (or any
// sibling of it) to corrupt. Every future src/section_*.c file should
// follow this same convention -- do not reintroduce a bool return on a
// tick() function stored in the DemoSection table.

#ifndef SECTION_COMMON_H
#define SECTION_COMMON_H

// Called by a section's own tick() once it reaches a natural end (e.g. a
// fade-out completing). main.c's run_section() checks and resets this
// flag once per iteration; a section that never naturally finishes (e.g.
// section_logo.c's indefinitely-circling raster bar) simply never calls
// this at all, relying only on its own min_ticks/max_ticks.
void section_mark_finished(void);

#endif // SECTION_COMMON_H
