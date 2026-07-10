# Makefile - oricdemo2026 (Oric Atmos, Oscar64)
#
# Two-step build:
#   1. Oscar64 (-n -tf=bin -rt=include/oric_crt.c) -> build/*.bin
#   2. tools/mktap.py -> build/*.tap
#
# make run requires Oricutron; see ORICUTRON_HOME below

# Bare 'make' must build the real tape demo (build/oricdemo.tap), per this
# project's own documented convention (see CLAUDE.md) -- without this,
# Make's default goal is simply the FIRST target textually defined below
# (the floppy pipeline's build/floppy_loader_placeholder.bin, since 'all:'
# itself is defined much further down), a real, confirmed discrepancy from
# the documented behaviour.
.DEFAULT_GOAL := all

# -------------------------------------------------------------------------
# Cross-platform portability (Windows CMD vs POSIX)
# -------------------------------------------------------------------------

ifeq ($(OS),Windows_NT)
  NULLDEV = nul:
  DEL     = -del /f
  RMDIR   = rmdir /s /q
  MKDIR   = mkdir
else
  NULLDEV = /dev/null
  DEL     = $(RM)
  RMDIR   = $(RM) -r
  MKDIR   = mkdir -p
endif

# -------------------------------------------------------------------------
# Toolchain
# -------------------------------------------------------------------------

ifndef OSCAR64_HOME
$(warning OSCAR64_HOME not set. Defaulting to $(HOME)/oscar64)
export OSCAR64_HOME = $(HOME)/oscar64
endif

ifndef ORICUTRON_HOME
$(warning ORICUTRON_HOME not set. Defaulting to $(HOME)/oricutron)
export ORICUTRON_HOME = $(HOME)/oricutron
endif

CC    = $(OSCAR64_HOME)/bin/oscar64
PY    = python3
EMUL  = $(ORICUTRON_HOME)/oricutron

# -------------------------------------------------------------------------
# Build versioning
# -------------------------------------------------------------------------

VERSION_MAJOR = 0
VERSION_MINOR = 1
VERSION_PATCH = 0

# -------------------------------------------------------------------------
# Project
# -------------------------------------------------------------------------

MAIN      = oricdemo
PROGNAME  = ORICDEMO
LOAD_ADDR = 0x0500

# -------------------------------------------------------------------------
# Compiler flags -- the real demo (src/main.c) builds against the HIRES
# runtime, not the default oric_crt.c: it's a sequencer over src/section_*.c
# effect modules (see MAIN_SRCS below), and HIRES bitmap graphics require
# include/oric_crt_hires.c's shrunk memory layout (see include/hires.h's
# header comment). -i=assets picks up asset headers like assets/bird.h.
# -------------------------------------------------------------------------

CFLAGS = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt_hires.c \
  -i=include      \
  -i=src          \
  -i=assets       \
  -O2             \
  -dNOFLOAT       \
  -dVERSION_MAJOR=$(VERSION_MAJOR) \
  -dVERSION_MINOR=$(VERSION_MINOR) \
  -dVERSION_PATCH=$(VERSION_PATCH)

# -------------------------------------------------------------------------
# Source dependency list
# Oscar64 follows #pragma compile chains internally; make does not --
# list everything here so rebuilds trigger correctly.
# -------------------------------------------------------------------------

MAIN_SRCS = \
  src/main.c            \
  src/section_background.c \
  src/section_background.h \
  src/section_clouds.c  \
  src/section_clouds.h  \
  src/section_bird.c    \
  src/section_bird.h    \
  assets/bird.h         \
  assets/popcorn.pt3    \
  include/oric_crt_hires.c \
  include/crt_math.c    \
  include/oric.h        \
  include/hires.c       \
  include/hires.h       \
  include/sprite.c      \
  include/sprite.h      \
  include/fixedmath.c   \
  include/fixedmath.h   \
  include/pt3.c         \
  include/pt3.h         \
  include/ay.c          \
  include/ay.h          \
  include/rasterirq.c   \
  include/rasterirq.h   \
  include/loci.c        \
  include/loci.h

# -------------------------------------------------------------------------
# Build-chain regression test (src/buildtest.c, default oric_crt.c runtime)
# -- what used to live in src/main.c before it became the real HIRES demo
# above. Not part of 'all'/'usb'/'zip'; only 'make test' builds this (via
# sandbox-reset's dependency on build/buildtest.tap below), and 'make
# run-phos' launches it visually in Phosphoric (it's the target that
# actually exercises LOCI, which the real demo no longer touches).
# -------------------------------------------------------------------------

MAIN_BUILDTEST     = buildtest
PROGNAME_BUILDTEST = BUILDTEST

CFLAGS_BUILDTEST = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt.c \
  -i=include      \
  -i=src          \
  -O2             \
  -dNOFLOAT       \
  -dVERSION_MAJOR=$(VERSION_MAJOR) \
  -dVERSION_MINOR=$(VERSION_MINOR) \
  -dVERSION_PATCH=$(VERSION_PATCH)

MAIN_BUILDTEST_SRCS = \
  src/buildtest.c       \
  src/strings.h         \
  src/strings_en.h      \
  include/oric_crt.c    \
  include/crt_math.c    \
  include/oric.h        \
  include/charwin.c     \
  include/charwin.h     \
  include/keyboard.c    \
  include/keyboard.h    \
  include/charset.c     \
  include/charset.h     \
  include/ijk.c         \
  include/ijk.h         \
  include/loci.c        \
  include/loci.h        \
  include/rasterirq.c   \
  include/rasterirq.h   \
  include/ay.c          \
  include/ay.h          \
  include/pt3.c         \
  include/pt3.h

# -------------------------------------------------------------------------
# HIRES test fixture -- separate program, built with the alternate
# oric_crt_hires.c runtime (shrunk main/stack regions, see that file).
# Not part of 'all'/'zip'/'usb' -- only used by 'make test-hires'.
# -------------------------------------------------------------------------

MAIN_HIRES       = hires_test
PROGNAME_HIRES   = HIRESTEST

CFLAGS_HIRES = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt_hires.c \
  -i=include      \
  -i=src          \
  -i=tests/fixtures \
  -O2             \
  -dNOFLOAT

MAIN_HIRES_SRCS = \
  src/hires_test.c        \
  include/oric_crt_hires.c \
  include/crt_math.c     \
  include/oric.h         \
  include/hires.c        \
  include/hires.h        \
  include/ttf.c          \
  include/ttf.h          \
  include/fixedmath.c    \
  include/fixedmath.h    \
  include/sprite.c       \
  include/sprite.h       \
  include/dissolve.c     \
  include/dissolve.h     \
  include/rasterirq.c    \
  include/rasterirq.h    \
  include/ay.c           \
  include/ay.h           \
  tests/fixtures/ttf_test_font.h

# -------------------------------------------------------------------------
# Floppy-disk build target (see docs/floppy.md) -- an independent second
# distribution target alongside the tape/LOCI 'all'/'usb'/'zip' pipeline
# above (that pipeline is completely unchanged by any of this). Boots via
# the Microdisc disk controller's own boot EPROM -- no SEDORIC/DOS, no
# LOCI device, works in plain Oricutron. Not part of 'all'/'usb'/'zip'.
#
# FLOPPY_LOADER_ADDRESS is the one genuinely shared constant between
# tools/floppy/bootsector_microdisc.c, tools/floppy/loader.c, and
# tools/floppy/disk_script.txt -- passed as a -d/-D define to all three so
# it can't drift. Boot-sector/loader track/sector POSITIONS (track 0
# sector 2 for the boot sector, track 0 sector 4 for the loader) are NOT
# threaded through Makefile variables -- they're literals independently
# matched between bootsector_microdisc.c's own #defines and
# disk_script.txt's SetPosition calls (documented in both places as
# "must match"), the same manual-sync convention already used for those
# two files' FDC register constants. If you change one, change the other.
# -------------------------------------------------------------------------

FLOPPY_LOADER_ADDRESS  = 0xFA00
# The demo's ENTRY point must be its own runtime's "startup" region ($0500,
# where oric_startup's SEI/BSS-clear/ZP-clear/stack-setup lives), NOT
# $0580 (the "main" region, i.e. main() itself) -- jumping straight to
# $0580 skips all of that init, leaving interrupts enabled and the
# software stack/BSS/ZP uninitialized. tools/floppy/loader.c defaults
# DEMO_ADDRESS to $0580 if never told otherwise, so this MUST be passed
# explicitly to every loader.c compile below (confirmed empirically: the
# demo hung early, with an interrupt vectoring into uninitialized zero-page
# garbage at $0245, before this fix). Shared by both pipelines below (real
# demo and floppytest regression) -- same convention either way.
FLOPPY_DEMO_ENTRY_ADDRESS = 0x0500

CFLAGS_FLOPPY_RT = \
  -n              \
  -tf=bin         \
  -i=include      \
  -O1

# ===========================================================================
# PIPELINE 1: the REAL demo (bird + background + music), on
# include/oric_crt_floppy_hires.c -- what 'disk'/'run-disk' build. Uses the
# ORIGINAL simple artifact names (build/floppy_*.bin, build/oricdemo_floppy.dsk)
# since this is the primary, user-facing floppy target, matching how
# src/main.c (not src/buildtest.c) owns the plain 'build/oricdemo.tap' name
# on the tape/LOCI side.
# ===========================================================================

FLOPPY_PROGNAME        = ORICDEMO

CFLAGS_FLOPPY_DEMO = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt_floppy_hires.c \
  -i=include      \
  -i=src          \
  -i=assets       \
  -O2             \
  -dNOFLOAT       \
  -dSTORAGE_FLOPPY

FLOPPY_SRCS = \
  src/main.c               \
  src/section_background.c \
  src/section_background.h \
  src/section_clouds.c     \
  src/section_clouds.h     \
  src/section_bird.c       \
  src/section_bird.h       \
  assets/bird.h            \
  assets/popcorn.pt3      \
  include/oric_crt_floppy_hires.c \
  include/crt_math.c       \
  include/oric.h           \
  include/hires.c          \
  include/hires.h          \
  include/sprite.c         \
  include/sprite.h         \
  include/fixedmath.c      \
  include/fixedmath.h      \
  include/floppy.c         \
  include/floppy.h         \
  include/rasterirq.c      \
  include/rasterirq.h      \
  include/ay.c             \
  include/ay.h             \
  include/pt3.c            \
  include/pt3.h

FLOPPY_LOADER_SRCS = tools/floppy/loader.c
FLOPPY_BOOTSECTOR_SRCS = tools/floppy/bootsector_microdisc.c

# File index 0 = the demo itself (boot handoff); 1 = assets/popcorn.pt3,
# via pt3_load(1) (STORAGE_FLOPPY) -- see src/main.c's MUSIC_FILE and
# tools/floppy/disk_script_demo.txt.
FLOPPY_MUSIC_BIN = assets/popcorn.pt3

# -------------------------------------------------------------------------
# Two-pass build (see docs/floppy.md):
#   1. Compile loader.c with placeholder demo track/sector/size (its
#      compiled SIZE is stable across both passes -- only immediate-
#      operand VALUES differ, not instruction count/structure -- so this
#      placeholder compile's size can be trusted for the disk layout).
#   2. Compile the demo with a checked-in placeholder floppy_directory.h
#      (same array shapes, dummy values) to learn its real compiled size.
#   3. Extract the boot sector (tools/floppy/extract_bootsector.py):
#      pulls just the "bootsector" label's bytes out of its own compiled
#      .bin/.map (which Oscar64's default runtime wraps in CRT scaffolding
#      this sector doesn't need) and prepends the Microdisc EPROM's
#      required 23-byte header.
#   4. Run oric_floppybuilder.py `init`: places all files, generates the
#      REAL build/floppy_directory.h (with the demo's real track/sector/
#      size -- needed by step 5) and the real (unused at this stage) init
#      disk image.
#   5. Recompile loader.c with the REAL demo track/sector/size from step
#      4's header (same size as step 1's placeholder compile, by design).
#   6. Recompile the demo with the REAL floppy_directory.h from step 4
#      (same size as step 2's placeholder compile, by design -- only the
#      embedded table VALUES differ, not the array shapes).
#   7. Run oric_floppybuilder.py `build`: same script as step 4, now with
#      the REAL loader (step 5) and REAL demo (step 6) -- their sizes are
#      unchanged from what step 4 already computed the layout from, so
#      the disk's track/sector placements stay valid.
# -------------------------------------------------------------------------

build/floppy_loader_placeholder.bin: $(FLOPPY_LOADER_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS_FLOPPY_RT) -rt=include/crt_math.c \
	    -dDEMO_TRACK=0 -dDEMO_SECTOR=0 -dDEMO_SIZE=0 \
	    -dLOADER_ADDRESS=$(FLOPPY_LOADER_ADDRESS) \
	    -dDEMO_ADDRESS=$(FLOPPY_DEMO_ENTRY_ADDRESS) \
	    -o=build/floppy_loader_placeholder.bin tools/floppy/loader.c

build/floppy_demo_pass1.bin: $(FLOPPY_SRCS) tests/fixtures/floppy_directory_placeholder.h
	@$(MKDIR) build 2>$(NULLDEV) ; true
	cp tests/fixtures/floppy_directory_placeholder.h build/floppy_directory.h
	$(CC) $(CFLAGS_FLOPPY_DEMO) -i=build \
	    -o=build/floppy_demo_pass1.bin src/main.c

build/floppy_bootsector_compiled.bin: $(FLOPPY_BOOTSECTOR_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS_FLOPPY_RT) -g \
	    -o=build/floppy_bootsector_compiled.bin tools/floppy/bootsector_microdisc.c

build/floppy_bootsector.bin: build/floppy_bootsector_compiled.bin
	$(PY) tools/floppy/extract_bootsector.py \
	    build/floppy_bootsector_compiled.bin \
	    build/floppy_bootsector_compiled.map \
	    build/floppy_bootsector.bin

# init pass: computes the real disk layout. Depends on the PLACEHOLDER
# loader/demo (their sizes, not their real values, are what matter here).
# NOTE: oric_floppybuilder.py resolves script-relative paths against the
# SCRIPT's own directory (tools/floppy/), not the caller's cwd -- so every
# -D path here is made absolute via $(CURDIR) to sidestep that entirely.
build/floppy_directory.h: build/floppy_loader_placeholder.bin build/floppy_demo_pass1.bin build/floppy_bootsector.bin tools/floppy/disk_script_demo.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	$(PY) tools/oric_floppybuilder.py init tools/floppy/disk_script_demo.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/floppy_init.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppy_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppy_loader_placeholder.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppy_demo_pass1.bin \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPY_MUSIC_BIN)

# Real values, parsed out of the generated header by the shell (make has
# no built-in way to read a C #define -- this is simpler than teaching
# oric_floppybuilder.py to also emit a Make-include fragment).
FLOPPY_DEMO_REAL_TRACK  = $(shell grep FloppyFileStartTrack  build/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')
FLOPPY_DEMO_REAL_SECTOR = $(shell grep FloppyFileStartSector build/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')

# The demo's REAL compiled size, read directly off build/floppy_demo.bin
# itself -- NOT floppy_directory.h's FloppyFileSize (which is derived from
# build/floppy_demo_pass1.bin, the PLACEHOLDER-header compile). The
# Makefile's own "two-pass build" design comment above assumes pass1 and
# the final demo compile to the SAME size ("only the embedded table VALUES
# differ, not the array shapes") -- that assumption is NOT reliably true
# (Oscar64 can and does emit a different-sized binary for the two passes;
# confirmed empirically, a real, previously-undiscovered bug: the gap was
# 8 bytes before this fix, silently landing in trailing padding, but grew
# to 56 bytes with an unrelated pt3.c change, landing squarely in
# pt3_tick()'s own `bitshift` lookup table -- LoadData's compile-time
# DEMO_SIZE then stopped 56 bytes short of the disk's actual demo data,
# permanently truncating `bitshift`'s tail to whatever RAM garbage was
# already there. This is what caused a confusing, extensively-investigated
# floppy-only PT3 mixer-register regression -- see project memory
# project_pt3_sample_select_bug for the full trace). Reading the real
# file's own size makes this correct regardless of whether/how much the
# two passes' sizes ever drift apart.
build/floppy_loader.bin: build/floppy_directory.h build/floppy_demo.bin $(FLOPPY_LOADER_SRCS)
	$(CC) $(CFLAGS_FLOPPY_RT) -rt=include/crt_math.c \
	    -dDEMO_TRACK=$(FLOPPY_DEMO_REAL_TRACK) \
	    -dDEMO_SECTOR=$(FLOPPY_DEMO_REAL_SECTOR) \
	    -dDEMO_SIZE=$(shell wc -c < build/floppy_demo.bin | tr -d ' ') \
	    -dLOADER_ADDRESS=$(FLOPPY_LOADER_ADDRESS) \
	    -dDEMO_ADDRESS=$(FLOPPY_DEMO_ENTRY_ADDRESS) \
	    -o=build/floppy_loader.bin tools/floppy/loader.c

build/floppy_demo.bin: build/floppy_directory.h $(FLOPPY_SRCS)
	$(CC) $(CFLAGS_FLOPPY_DEMO) -i=build \
	    -o=build/floppy_demo.bin src/main.c

build/oricdemo_floppy.dsk: build/floppy_loader.bin build/floppy_demo.bin build/floppy_bootsector.bin tools/floppy/disk_script_demo.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	$(PY) tools/oric_floppybuilder.py build tools/floppy/disk_script_demo.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/oricdemo_floppy.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppy_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppy_loader.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppy_demo.bin \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPY_MUSIC_BIN)

disk: build/oricdemo_floppy.dsk

run-disk: build/oricdemo_floppy.dsk
	cd $(ORICUTRON_HOME) && \
	    $(EMUL) $(EMUFLAG) --disk-rom microdis.rom -d "$(CURDIR)/build/oricdemo_floppy.dsk"

# ===========================================================================
# PIPELINE 2: src/floppy_test.c's own regression fixture, on the DEFAULT
# include/oric_crt_floppy.c runtime -- analogous to src/buildtest.c on the
# tape/LOCI side (see that section's own comment). NOT demo content; only
# 'make test-disk' builds this. Artifacts use a "floppytest_" prefix to
# avoid colliding with PIPELINE 1's plain names above.
# ===========================================================================

FLOPPYTEST_PROGNAME = FLOPPYDEMO

CFLAGS_FLOPPYTEST_DEMO = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt_floppy.c \
  -i=include      \
  -i=src          \
  -O2             \
  -dNOFLOAT       \
  -dSTORAGE_FLOPPY

FLOPPYTEST_SRCS = \
  src/floppy_test.c        \
  include/oric_crt_floppy.c \
  include/crt_math.c       \
  include/oric.h           \
  include/charwin.c        \
  include/charwin.h        \
  include/keyboard.c       \
  include/keyboard.h       \
  include/floppy.c         \
  include/floppy.h         \
  include/rasterirq.c      \
  include/rasterirq.h      \
  include/ay.c             \
  include/ay.h             \
  include/pt3.c            \
  include/pt3.h

# Test fixtures baked into the disk image (see src/floppy_test.c's own
# file-index convention comment: 0 = itself, 1 = payload, 2 = music).
FLOPPYTEST_PAYLOAD_BIN = tests/fixtures/floppy_payload_test.bin
FLOPPYTEST_MUSIC_BIN   = tests/fixtures/music.pt3

build/floppytest_loader_placeholder.bin: $(FLOPPY_LOADER_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS_FLOPPY_RT) -rt=include/crt_math.c \
	    -dDEMO_TRACK=0 -dDEMO_SECTOR=0 -dDEMO_SIZE=0 \
	    -dLOADER_ADDRESS=$(FLOPPY_LOADER_ADDRESS) \
	    -dDEMO_ADDRESS=$(FLOPPY_DEMO_ENTRY_ADDRESS) \
	    -o=build/floppytest_loader_placeholder.bin tools/floppy/loader.c

build/floppytest_demo_pass1.bin: $(FLOPPYTEST_SRCS) tests/fixtures/floppy_directory_placeholder.h
	@$(MKDIR) build/floppytest 2>$(NULLDEV) ; true
	cp tests/fixtures/floppy_directory_placeholder.h build/floppytest/floppy_directory.h
	$(CC) $(CFLAGS_FLOPPYTEST_DEMO) -i=build/floppytest \
	    -o=build/floppytest_demo_pass1.bin src/floppy_test.c

build/floppytest_bootsector.bin: build/floppy_bootsector_compiled.bin
	$(PY) tools/floppy/extract_bootsector.py \
	    build/floppy_bootsector_compiled.bin \
	    build/floppy_bootsector_compiled.map \
	    build/floppytest_bootsector.bin

build/floppytest/floppy_directory.h: build/floppytest_loader_placeholder.bin build/floppytest_demo_pass1.bin build/floppytest_bootsector.bin tools/floppy/disk_script.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	@$(MKDIR) build/floppytest 2>$(NULLDEV) ; true
	$(PY) tools/oric_floppybuilder.py init tools/floppy/disk_script.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppytest/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/floppytest_init.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppytest_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppytest_loader_placeholder.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppytest_demo_pass1.bin \
	    -D PAYLOAD_BIN=$(CURDIR)/$(FLOPPYTEST_PAYLOAD_BIN) \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPYTEST_MUSIC_BIN)

FLOPPYTEST_DEMO_REAL_TRACK  = $(shell grep FloppyFileStartTrack  build/floppytest/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')
FLOPPYTEST_DEMO_REAL_SECTOR = $(shell grep FloppyFileStartSector build/floppytest/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')

# See build/floppy_loader.bin's own comment above (identical rationale,
# same bug class, its own regression fixture pipeline) -- real file size,
# not the pass1-derived FloppyFileSize.
build/floppytest_loader.bin: build/floppytest/floppy_directory.h build/floppytest_demo.bin $(FLOPPY_LOADER_SRCS)
	$(CC) $(CFLAGS_FLOPPY_RT) -rt=include/crt_math.c \
	    -dDEMO_TRACK=$(FLOPPYTEST_DEMO_REAL_TRACK) \
	    -dDEMO_SECTOR=$(FLOPPYTEST_DEMO_REAL_SECTOR) \
	    -dDEMO_SIZE=$(shell wc -c < build/floppytest_demo.bin | tr -d ' ') \
	    -dLOADER_ADDRESS=$(FLOPPY_LOADER_ADDRESS) \
	    -dDEMO_ADDRESS=$(FLOPPY_DEMO_ENTRY_ADDRESS) \
	    -o=build/floppytest_loader.bin tools/floppy/loader.c

build/floppytest_demo.bin: build/floppytest/floppy_directory.h $(FLOPPYTEST_SRCS)
	$(CC) $(CFLAGS_FLOPPYTEST_DEMO) -i=build/floppytest \
	    -o=build/floppytest_demo.bin src/floppy_test.c

build/floppytest.dsk: build/floppytest_loader.bin build/floppytest_demo.bin build/floppytest_bootsector.bin tools/floppy/disk_script.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	$(PY) tools/oric_floppybuilder.py build tools/floppy/disk_script.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppytest/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/floppytest.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppytest_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppytest_loader.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppytest_demo.bin \
	    -D PAYLOAD_BIN=$(CURDIR)/$(FLOPPYTEST_PAYLOAD_BIN) \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPYTEST_MUSIC_BIN)

test-disk: check-phosphoric build/floppytest.dsk
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	PHOS=$(PHOS) ATMOSROM=$(ATMOSROM) DISKROM=$(DISKROM) \
	    DSKFILE=build/floppytest.dsk OUT=tests/out \
	    bash tests/scripts/test_disk.sh

# -------------------------------------------------------------------------
# USB stick transfer -- variable declarations
# -------------------------------------------------------------------------
# Set USBPATH in .env (gitignored) -- path to the directory on the USB stick.
# Native Linux: /media/yourname/USBSTICK/oric
# WSL2: Windows drives auto-mount at /mnt/<letter>; USB stick on E: -> /mnt/e/oric
# See .env.example for a template.

-include .env
USBPATH  ?= NOT_SET

# Derived from USBPATH -- used for WSL2 auto-mount.
# Assumes USBPATH starts with /mnt/<letter>/... (standard WSL2 drvfs layout).
# Example: USBPATH=/mnt/e/oric -> USBMOUNT=/mnt/e, USBDRIVE=E:
USBMOUNT := $(shell echo "$(USBPATH)" | cut -d/ -f1-3)
USBDRIVE := $(shell echo "$(USBPATH)" | cut -d/ -f3 | tr a-z A-Z):

# Detect WSL2 at parse time so check-usb can branch without a shell function.
IS_WSL2  := $(shell grep -qi microsoft /proc/version 2>/dev/null && echo 1 || echo 0)

# -------------------------------------------------------------------------
# Emulator flags
# -------------------------------------------------------------------------

EMUFLAG = -ma --serial none --vsynchack off --turbotape on

# -------------------------------------------------------------------------
# Phosphoric automated testing
# -------------------------------------------------------------------------
# PHOSDIR is set in .env (see .env.example) -- checkout of
# https://github.com/benedictemarty/Phosphoric, providing oric1-emu and
# roms/basic11b.rom. Phosphoric lets oricdemo.tap be fast-loaded (-t ... -f)
# under Atmos BASIC 1.1 and tested headless via RAM dumps.

PHOSDIR  ?= NOT_SET
PHOS      = $(PHOSDIR)/oric1-emu
ATMOSROM  = $(PHOSDIR)/roms/basic11b.rom

# Microdisc boot EPROM ROM image, needed only by `make test-disk` (see
# docs/floppy.md) -- Phosphoric's --disk-rom flag. Confirmed present
# locally under ORICUTRON_HOME; Phosphoric itself does not ship it (it only
# emulates the Microdisc hardware, not its firmware).
DISKROM   = $(ORICUTRON_HOME)/roms/microdis.rom

CYCLES   ?= 8000000

# =========================================================================
# Targets
# all: must appear first so it is the default goal
# =========================================================================

.PHONY: all clean run run-phos run-phos-buildtest docs zip check-usb usb check-phosphoric sandbox-reset test-capture test-boot test test-hires check-pictconv test-pictconv disk run-disk test-disk

all: build/$(MAIN).tap

# Step 1: compile main app to raw binary
build/$(MAIN).bin: $(MAIN_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS) -o=build/$(MAIN).bin src/main.c

# Step 2: wrap binary in Oric tape header
build/$(MAIN).tap: build/$(MAIN).bin
	$(PY) tools/mktap.py \
	    build/$(MAIN).bin \
	    build/$(MAIN).tap \
	    $(PROGNAME) \
	    $(LOAD_ADDR)

# Build-chain regression test -- see MAIN_BUILDTEST_SRCS/CFLAGS_BUILDTEST above.
build/$(MAIN_BUILDTEST).bin: $(MAIN_BUILDTEST_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS_BUILDTEST) -o=build/$(MAIN_BUILDTEST).bin src/buildtest.c

build/$(MAIN_BUILDTEST).tap: build/$(MAIN_BUILDTEST).bin
	$(PY) tools/mktap.py \
	    build/$(MAIN_BUILDTEST).bin \
	    build/$(MAIN_BUILDTEST).tap \
	    $(PROGNAME_BUILDTEST) \
	    $(LOAD_ADDR)

# HIRES test fixture -- see MAIN_HIRES_SRCS/CFLAGS_HIRES above.
build/$(MAIN_HIRES).bin: $(MAIN_HIRES_SRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS_HIRES) -o=build/$(MAIN_HIRES).bin src/hires_test.c

build/$(MAIN_HIRES).tap: build/$(MAIN_HIRES).bin
	$(PY) tools/mktap.py \
	    build/$(MAIN_HIRES).bin \
	    build/$(MAIN_HIRES).tap \
	    $(PROGNAME_HIRES) \
	    $(LOAD_ADDR)

# Launch in Oricutron (must cd to oricutron dir -- it loads ROMs from cwd).
run: build/$(MAIN).tap
	cd $(ORICUTRON_HOME) && \
	    $(EMUL) $(EMUFLAG) "$(CURDIR)/build/$(MAIN).tap"

# Launch the real demo (build/$(MAIN).tap, tape/LOCI target) visually in
# Phosphoric instead of Oricutron (fast-loads the tape, auto-runs, and
# mounts assets/ as the LOCI flash root so pt3_load()'s "popcorn.pt3"
# resolves). Phosphoric DOES emulate real AY audio -- this is just as valid
# a way to see/hear the real demo as Oricutron's own 'make run'.
# Needs PHOSDIR in .env -- see check-phosphoric. Not headless: opens a real
# emulator window; close it or Ctrl+C in the terminal to quit. Requires the
# oric1-emu binary itself to have been built with 'make SDL2=1' in the
# Phosphoric checkout -- a headless-only build opens no window and gives no
# error about it.
run-phos: check-phosphoric build/$(MAIN).tap
	$(PHOS) -r $(ATMOSROM) \
	    -t build/$(MAIN).tap -f --loci-flash assets

# Launch the build-chain/LOCI regression test (src/buildtest.c) visually in
# Phosphoric -- the smoke-test equivalent of 'run-phos' above, exercising
# LOCI/IJK detection and the PT3 decode-correctness fixtures
# (tests/fixtures/music.pt3/music_effects.pt3), not the real demo.
run-phos-buildtest: check-phosphoric build/$(MAIN_BUILDTEST).tap
	$(PHOS) -r $(ATMOSROM) \
	    -t build/$(MAIN_BUILDTEST).tap -f --loci-flash tests/fixtures

# -------------------------------------------------------------------------
# USB stick transfer
# -------------------------------------------------------------------------

check-usb:
	@test "$(USBPATH)" != "NOT_SET" || \
	    (echo "ERROR: USBPATH not set -- copy .env.example to .env and set USBPATH" && false)
	@if ! test -d "$(USBPATH)"; then \
	    if [ "$(IS_WSL2)" = "1" ]; then \
	        echo "WSL2: mounting $(USBDRIVE) at $(USBMOUNT) via drvfs..."; \
	        sudo mount -t drvfs $(USBDRIVE) $(USBMOUNT); \
	    fi; \
	fi
	@test -d "$(USBPATH)" || \
	    (echo "ERROR: USB path '$(USBPATH)' not found -- plug in USB stick and retry" && false)

usb: check-usb all
	cp build/$(MAIN).tap "$(USBPATH)/"
	cp assets/popcorn.pt3 "$(USBPATH)/"
	@if [ "$(IS_WSL2)" = "1" ]; then \
	    echo "WSL2: unmounting $(USBMOUNT)..."; \
	    sudo umount $(USBMOUNT); \
	    echo "Done -- USB stick can now be ejected in Windows."; \
	fi

# -------------------------------------------------------------------------
# Phosphoric automated testing
# -------------------------------------------------------------------------
# make test          -- full automated suite (currently: test-boot)
# make test-capture CYCLES=N TYPEKEYS='...'
#                    -- calibration helper: fast-loads buildtest.tap under
#                       Atmos BASIC 1.1, runs for CYCLES, dumps
#                       tests/out/capture.bin (RAM) and tests/out/capture.png
#                       (screenshot). No assertions -- used to find the
#                       right cycle counts / --type-keys sequences.

check-phosphoric:
	@test "$(PHOSDIR)" != "NOT_SET" || \
	    (echo "ERROR: PHOSDIR not set -- copy .env.example to .env and set PHOSDIR" && false)
	@test -x "$(PHOS)" || \
	    (echo "ERROR: oric1-emu not found/executable at $(PHOS) -- check PHOSDIR in .env" && false)
	@test -f "$(ATMOSROM)" || \
	    (echo "ERROR: Atmos ROM not found at $(ATMOSROM)" && false)

# Reset the test sandbox from checked-in fixtures + the freshly built
# buildtest tap, so every test run starts from a known state.
sandbox-reset: build/$(MAIN_BUILDTEST).tap
	$(RMDIR) tests/sandbox 2>$(NULLDEV) ; true
	$(MKDIR) tests/sandbox 2>$(NULLDEV) ; true
	cp -r tests/fixtures/. tests/sandbox/
	find tests/sandbox -name '.gitkeep' -delete
	cp build/$(MAIN_BUILDTEST).tap tests/sandbox/

test-capture: check-phosphoric sandbox-reset
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	$(PHOS) -r $(ATMOSROM) \
	    -t tests/sandbox/$(MAIN_BUILDTEST).tap -f --loci-flash tests/sandbox \
	    --headless -c $(CYCLES) \
	    $(if $(TYPEKEYS),--type-keys '$(TYPEKEYS)') \
	    --dump-ram-at $(CYCLES):tests/out/capture.bin \
	    --screenshot-at $(CYCLES):tests/out/capture.ppm
	@which pnmtopng >$(NULLDEV) 2>&1 && pnmtopng tests/out/capture.ppm > tests/out/capture.png || true
	python3 tests/scripts/oric_screen.py tests/out/capture.bin

test-boot: check-phosphoric sandbox-reset
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	PHOS=$(PHOS) ATMOSROM=$(ATMOSROM) SANDBOX=tests/sandbox OUT=tests/out \
	    TAPFILE=$(MAIN_BUILDTEST).tap \
	    bash tests/scripts/test_boot.sh

test:
	$(MAKE) test-boot
	$(MAKE) test-pictconv

# oric_pictconv.py unit test -- pure Python, no emulator, fast enough to
# fold into the default 'make test' (unlike test-hires, which needs a slow
# Phosphoric run and a second .tap build, so stays separate/opt-in).
check-pictconv:
	@python3 -c "import PIL" 2>$(NULLDEV) || \
	    (echo "ERROR: Pillow not installed -- run: pip install -r tools/requirements.txt" && false)

test-pictconv: check-pictconv
	python3 tests/scripts/test_pictconv.py

# HIRES library test (separate/opt-in -- not part of 'make test' since it
# needs a second .tap build with the alternate oric_crt_hires.c runtime).
sandbox-reset-hires: build/$(MAIN_HIRES).tap
	$(RMDIR) tests/sandbox 2>$(NULLDEV) ; true
	$(MKDIR) tests/sandbox 2>$(NULLDEV) ; true
	cp -r tests/fixtures/. tests/sandbox/
	find tests/sandbox -name '.gitkeep' -delete
	cp build/$(MAIN_HIRES).tap tests/sandbox/

test-hires: check-phosphoric sandbox-reset-hires
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	PHOS=$(PHOS) ATMOSROM=$(ATMOSROM) SANDBOX=tests/sandbox OUT=tests/out \
	    TAPFILE=$(MAIN_HIRES).tap \
	    bash tests/scripts/test_hires.sh

# -------------------------------------------------------------------------
# Documentation -- generate PDF from Markdown (requires pandoc)
# -------------------------------------------------------------------------

docs: README.pdf

README.pdf: README.md
	@if which pandoc >/dev/null 2>&1; then \
	    pandoc README.md -o README.pdf; \
	else \
	    echo "WARNING: pandoc not found -- README.pdf not updated (install: sudo apt install pandoc texlive-xetex)"; \
	fi

# -------------------------------------------------------------------------
# Release ZIP -- same payload as 'make usb', plus the PDF README.
# ZIP name: oricdemo2026_vMAJOR.MINOR.PATCH_YYYYMMDD.zip
# -------------------------------------------------------------------------

ZIPNAME = oricdemo2026_v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)_$(shell date +%Y%m%d)

zip: all docs
	$(MKDIR) build 2>$(NULLDEV) ; true
	zip -j build/$(ZIPNAME).zip \
	    build/$(MAIN).tap \
	    assets/popcorn.pt3 \
	    README.pdf
	@echo "Created build/$(ZIPNAME).zip"

# -------------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------------

clean:
	$(DEL) build/$(MAIN).bin 2>$(NULLDEV) ; true
	$(DEL) build/$(MAIN).tap 2>$(NULLDEV) ; true
	$(DEL) build/$(MAIN_BUILDTEST).bin 2>$(NULLDEV) ; true
	$(DEL) build/$(MAIN_BUILDTEST).tap 2>$(NULLDEV) ; true
