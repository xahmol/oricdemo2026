# Makefile - oricdemo2026 (Oric Atmos, Oscar64)
#
# Two-step build:
#   1. Oscar64 (-n -tf=bin -rt=include/oric_crt.c) -> build/*.bin
#   2. tools/mktap.py -> build/*.tap
#
# make run requires Oricutron; see ORICUTRON_HOME below

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
# Compiler flags
# -------------------------------------------------------------------------

CFLAGS = \
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

# -------------------------------------------------------------------------
# Source dependency list
# Oscar64 follows #pragma compile chains internally; make does not --
# list everything here so rebuilds trigger correctly.
# -------------------------------------------------------------------------

MAIN_SRCS = \
  src/main.c            \
  src/strings.h         \
  src/strings_en.h       \
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

FLOPPY_MAIN            = floppy_test
FLOPPY_PROGNAME        = FLOPPYDEMO
FLOPPY_LOADER_ADDRESS  = 0xFA00
# The demo's ENTRY point must be include/oric_crt_floppy.c's "startup"
# region ($0500, where oric_startup's SEI/BSS-clear/ZP-clear/stack-setup
# lives), NOT $0580 (the "main" region, i.e. main() itself) -- jumping
# straight to $0580 skips all of that init, leaving interrupts enabled and
# the software stack/BSS/ZP uninitialized. tools/floppy/loader.c defaults
# DEMO_ADDRESS to $0580 if never told otherwise, so this MUST be passed
# explicitly to every loader.c compile below (confirmed empirically: the
# demo hung early, with an interrupt vectoring into uninitialized zero-page
# garbage at $0245, before this fix).
FLOPPY_DEMO_ENTRY_ADDRESS = 0x0500

CFLAGS_FLOPPY_RT = \
  -n              \
  -tf=bin         \
  -i=include      \
  -O1

CFLAGS_FLOPPY_DEMO = \
  -n              \
  -tf=bin         \
  -rt=include/oric_crt_floppy.c \
  -i=include      \
  -i=src          \
  -O2             \
  -dNOFLOAT       \
  -dSTORAGE_FLOPPY

FLOPPY_SRCS = \
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

FLOPPY_LOADER_SRCS = tools/floppy/loader.c
FLOPPY_BOOTSECTOR_SRCS = tools/floppy/bootsector_microdisc.c

# Test fixtures baked into the disk image (see src/floppy_test.c's own
# file-index convention comment: 0 = itself, 1 = payload, 2 = music).
FLOPPY_PAYLOAD_BIN = tests/fixtures/floppy_payload_test.bin
FLOPPY_MUSIC_BIN   = tests/fixtures/music.pt3

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
	    -o=build/floppy_demo_pass1.bin src/floppy_test.c

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
build/floppy_directory.h: build/floppy_loader_placeholder.bin build/floppy_demo_pass1.bin build/floppy_bootsector.bin tools/floppy/disk_script.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	$(PY) tools/oric_floppybuilder.py init tools/floppy/disk_script.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/floppy_init.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppy_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppy_loader_placeholder.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppy_demo_pass1.bin \
	    -D PAYLOAD_BIN=$(CURDIR)/$(FLOPPY_PAYLOAD_BIN) \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPY_MUSIC_BIN)

# Real values, parsed out of the generated header by the shell (make has
# no built-in way to read a C #define -- this is simpler than teaching
# oric_floppybuilder.py to also emit a Make-include fragment).
FLOPPY_DEMO_REAL_TRACK  = $(shell grep FloppyFileStartTrack  build/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')
FLOPPY_DEMO_REAL_SECTOR = $(shell grep FloppyFileStartSector build/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')
FLOPPY_DEMO_REAL_SIZE   = $(shell grep FloppyFileSize        build/floppy_directory.h | sed -n 's/.*{ *\([0-9]*\).*/\1/p')

build/floppy_loader.bin: build/floppy_directory.h $(FLOPPY_LOADER_SRCS)
	$(CC) $(CFLAGS_FLOPPY_RT) -rt=include/crt_math.c \
	    -dDEMO_TRACK=$(FLOPPY_DEMO_REAL_TRACK) \
	    -dDEMO_SECTOR=$(FLOPPY_DEMO_REAL_SECTOR) \
	    -dDEMO_SIZE=$(FLOPPY_DEMO_REAL_SIZE) \
	    -dLOADER_ADDRESS=$(FLOPPY_LOADER_ADDRESS) \
	    -dDEMO_ADDRESS=$(FLOPPY_DEMO_ENTRY_ADDRESS) \
	    -o=build/floppy_loader.bin tools/floppy/loader.c

build/floppy_demo.bin: build/floppy_directory.h $(FLOPPY_SRCS)
	$(CC) $(CFLAGS_FLOPPY_DEMO) -i=build \
	    -o=build/floppy_demo.bin src/floppy_test.c

build/oricdemo_floppy.dsk: build/floppy_loader.bin build/floppy_demo.bin build/floppy_bootsector.bin tools/floppy/disk_script.txt tools/floppy/directory_sanity_sector.bin tools/floppy/sector1_header.bin
	$(PY) tools/oric_floppybuilder.py build tools/floppy/disk_script.txt \
	    -D LAYOUT_HEADER=$(CURDIR)/build/floppy_directory.h \
	    -D DISK_IMAGE=$(CURDIR)/build/oricdemo_floppy.dsk \
	    -D BOOTSECTOR_BIN=$(CURDIR)/build/floppy_bootsector.bin \
	    -D DIRECTORY_SANITY_BIN=$(CURDIR)/tools/floppy/directory_sanity_sector.bin \
	    -D SECTOR1_HEADER_BIN=$(CURDIR)/tools/floppy/sector1_header.bin \
	    -D LOADER_BIN=$(CURDIR)/build/floppy_loader.bin \
	    -D LOADER_LOAD_ADDR=$(FLOPPY_LOADER_ADDRESS) \
	    -D DEMO_BIN=$(CURDIR)/build/floppy_demo.bin \
	    -D PAYLOAD_BIN=$(CURDIR)/$(FLOPPY_PAYLOAD_BIN) \
	    -D MUSIC_BIN=$(CURDIR)/$(FLOPPY_MUSIC_BIN)

disk: build/oricdemo_floppy.dsk

run-disk: build/oricdemo_floppy.dsk
	cd $(ORICUTRON_HOME) && \
	    $(EMUL) $(EMUFLAG) --disk-rom microdis.rom -d "$(CURDIR)/build/oricdemo_floppy.dsk"

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

CYCLES   ?= 8000000

# =========================================================================
# Targets
# all: must appear first so it is the default goal
# =========================================================================

.PHONY: all clean run docs zip check-usb usb check-phosphoric sandbox-reset test-capture test-boot test test-hires check-pictconv test-pictconv

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
#                    -- calibration helper: fast-loads oricdemo.tap under
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

# Reset the test sandbox from checked-in fixtures + the freshly built tap,
# so every test run starts from a known state.
sandbox-reset: build/$(MAIN).tap
	$(RMDIR) tests/sandbox 2>$(NULLDEV) ; true
	$(MKDIR) tests/sandbox 2>$(NULLDEV) ; true
	cp -r tests/fixtures/. tests/sandbox/
	find tests/sandbox -name '.gitkeep' -delete
	cp build/$(MAIN).tap tests/sandbox/

test-capture: check-phosphoric sandbox-reset
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	$(PHOS) -r $(ATMOSROM) \
	    -t tests/sandbox/$(MAIN).tap -f --loci-flash tests/sandbox \
	    --headless -c $(CYCLES) \
	    $(if $(TYPEKEYS),--type-keys '$(TYPEKEYS)') \
	    --dump-ram-at $(CYCLES):tests/out/capture.bin \
	    --screenshot-at $(CYCLES):tests/out/capture.ppm
	@which pnmtopng >$(NULLDEV) 2>&1 && pnmtopng tests/out/capture.ppm > tests/out/capture.png || true
	python3 tests/scripts/oric_screen.py tests/out/capture.bin

test-boot: check-phosphoric sandbox-reset
	$(MKDIR) tests/out 2>$(NULLDEV) ; true
	PHOS=$(PHOS) ATMOSROM=$(ATMOSROM) SANDBOX=tests/sandbox OUT=tests/out \
	    TAPFILE=$(MAIN).tap \
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
	    README.pdf
	@echo "Created build/$(ZIPNAME).zip"

# -------------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------------

clean:
	$(DEL) build/$(MAIN).bin 2>$(NULLDEV) ; true
	$(DEL) build/$(MAIN).tap 2>$(NULLDEV) ; true
