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
