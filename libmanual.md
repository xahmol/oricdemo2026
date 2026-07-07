# oricdemo2026 — Library Reference Manual

Oscar64 bare-metal libraries for the Oric Atmos.
Target: 6502A, 1 MHz, no ROM calls.

Copied verbatim from [OricScreenEditorLOCI](https://github.com/xahmol/OricScreenEditorLOCI)'s `libmanual.md`
(itself adapted from [locifilemanager-v2's `libmanual.md`](https://github.com/xahmol/locifilemanager-v2/blob/main/libmanual.md)) — §1-5 cover the
libraries shared verbatim between the two projects (`oric.h`, `keyboard.h`,
`charwin.h`, `ijk.h`, `loci.h`), with notes where OSE's usage differs. §6
(`include/charset.h`) is new — a generic charset-bank library extracted
during OSE development. §7 (`menu.h`) is rewritten for OSE's smaller,
main-RAM-backed menu system. §8 (build notes) is adapted for OSE's Makefile.

---

## Contents

1. [Hardware overview (oric.h)](#1-hardware-overview-orich)
2. [Keyboard scanner (keyboard.h)](#2-keyboard-scanner-keyboardh)
3. [Character window library (charwin.h)](#3-character-window-library-charwinh)
4. [IJK joystick (ijk.h)](#4-ijk-joystick-ijkh)
5. [LOCI mass-storage API (loci.h)](#5-loci-mass-storage-api-locih)
6. [Generic charset library (include/charset.h)](#6-generic-charset-library-includecharseth)
7. [Menu system (menu.h)](#7-menu-system-menuh)
8. [Build notes](#8-build-notes)

---

## 1. Hardware overview (oric.h)

Include `oric.h` in any file that references hardware registers, attribute
constants, or screen layout macros.

### Screen

| Symbol | Value | Meaning |
|---|---|---|
| `TEXTVRAM` | `0xBB80` | Base address of text screen RAM |
| `SCREEN_COLS` | `40` | Columns per row |
| `SCREEN_ROWS` | `28` | Rows |
| `SCREEN_SIZE` | `1120` | Total bytes (40 × 28) |

The Oric ULA processes screen RAM left-to-right on every raster line. A byte
with `(byte & 0x60) == 0` (i.e. values `0x00–0x1F`) is a **serial attribute**
that changes ink colour, paper colour, or character set for the rest of that
row.  All other byte values are character codes.  The ULA resets ink to white
and paper to black at the start of each raster line.

**Consequence:** attributes occupy one screen column each and shift all
subsequent characters one position to the right.

### Ink (foreground) colour constants

`A_FWBLACK` (0) · `A_FWRED` (1) · `A_FWGREEN` (2) · `A_FWYELLOW` (3)
`A_FWBLUE` (4) · `A_FWMAGENTA` (5) · `A_FWCYAN` (6) · `A_FWWHITE` (7)

**Warning:** `A_FWBLACK = 0x00` is the C NUL terminator.  It cannot be
embedded in a string literal.  Write it with `cwin_put_attr(&w, A_FWBLACK)`.

### Paper (background) colour constants

`A_BGBLACK` (16) · `A_BGRED` (17) · `A_BGGREEN` (18) · `A_BGYELLOW` (19)
`A_BGBLUE` (20) · `A_BGMAGENTA` (21) · `A_BGCYAN` (22) · `A_BGWHITE` (23)

### Character-set mode constants

| Constant | Value | Effect |
|---|---|---|
| `A_STD` | 8 | Standard character set |
| `A_ALT` | 9 | Alternate (mosaic/graphics) set |
| `A_STD2H` | 10 | Double-height standard |
| `A_ALT2H` | 11 | Double-height alternate |
| `A_BLINKSTD` | 12 | Blinking standard |
| `A_BLINKALT` | 13 | Blinking alternate |
| `A_BLINK2H` | 14 | Double-height blinking |
| `A_BLINK2HALT` | 15 | Double-height blinking alternate |

### Special character codes

| Constant | Value | Character |
|---|---|---|
| `CH_SPACE` | `0x20` | Space — safe for clearing |
| `CH_INVSPACE` | `0xA0` | Solid ink-colour block (cursor, animation) |
| `CH_POUND` | `0x5F` | £ sign (ROM maps ASCII `_`) |
| `CH_COPYRIGHT` | `0x60` | © sign (ROM maps ASCII `` ` ``) |

Avoid `_` and `` ` `` in display strings; they render as £ and ©.

### Character set RAM (CHARSET_STD / CHARSET_ALT / CHARSETROM)

OSE-specific addition to `oric.h` (not present in locifilemanager-v2's
copy). The Oric's ULA renders text glyphs from **live RAM**, not ROM —
redefining the bytes below takes effect on the very next raster line, no
special handling needed.

| Symbol | Value | Meaning |
|---|---|---|
| `CHARSET_STD` | `0xB400` | Standard charset bank base (codes `0x00`-`0x7F`) |
| `CHARSET_ALT` | `0xB800` | Alternate charset bank base (codes `0x00`-`0x7F`) |
| `CHARSETROM` | `0xFC78` | ROM source for the standard charset (96 printable codes only, see below) |

Each bank is 1024 bytes = 128 glyphs (codes `0x00`-`0x7F`) × 8 bytes (one per
pixel row); bits 5..0 of each byte are pixels left-to-right (bit5 = leftmost
of the 6 visible columns — Oric chars are 6×8).

**Two different addressing conventions:**

- `CHARSET_STD`/`CHARSET_ALT`: full 128-glyph banks, glyph address =
  `base + screencode*8`, **no offset**. Codes `0x00-0x1F` are present in RAM
  but never displayed (those screen-RAM byte values trigger attribute mode
  instead of a glyph lookup).
- `CHARSETROM`: only the 96 printable codes `0x20-0x7F` (768 bytes), glyph
  address = `CHARSETROM + (screencode-0x20)*8` — i.e. **with** a `-0x20`
  offset. This is the source for the "restore standard charset" command
  (standard set only — see §6's charset-swap mechanism).

`include/charset.h` (§6 below) provides addressing/copy primitives and
glyph-bitmap transforms over this layout, used by the character editor
(`src/charsetedit.c`) and the popup charset-swap mechanism
(`src/charsetswap.c`).

### Inline string attribute macros (ASTR_*)

Embed colour or charset changes directly in string literals:

```c
cwin_putat_string(&w, 2, 5,
    ASTR_INK_RED "alert" ASTR_INK_WHITE " normal");
```

Each `ASTR_*` expands to a one-byte escape sequence.  Available macros:

- Ink: `ASTR_INK_RED` · `_GREEN` · `_YELLOW` · `_BLUE` · `_MAGENTA` · `_CYAN` · `_WHITE`
- Paper: `ASTR_PAPER_BLACK` · `_RED` · `_GREEN` · `_YELLOW` · `_BLUE` · `_MAGENTA` · `_CYAN` · `_WHITE`
- Charset: `ASTR_CHARSET_STD` · `_ALT` · `_STD2H` · `_ALT2H` · `_BLKSTD` · `_BLKALT` · `_BLK2H` · `_BLK2HA`

**Constraints:**
- `ASTR_INK_BLACK` (`\x00`) = NUL — cannot appear in a C string literal.
  Use `cwin_put_attr(&w, A_FWBLACK)`.
- `ASTR_CHARSET_STD2H` (`\x0A`) = `\n` — triggers scroll in
  `cwin_console_put_string`. Use `cwin_put_attr` in console context.

### Overlay RAM

Overlay RAM (`0xC000–0xFFFF`) is normally the Atmos ROM.  When LOCI is
connected, writing to the Microdisc-compatible register at `0x0314` enables
RAM underneath the ROM.

```c
#define MICRODISCCFG    (*((volatile uint8_t *)0x0314))
#define OVERLAY_ON      0xFD   // enable overlay RAM
#define OVERLAY_OFF     0xFF   // restore ROM
#define OVERLAY_BASE    0xC000U
#define OVERLAY_SIZE    0x4000U
```

**Requires LOCI.** Not testable in Oricutron.  The high-level helpers
`enable_overlay_ram()` and `disable_overlay_ram()` in `loci.h` (§5) are the
preferred interface — currently unused in OSE (see §5's note).

---

## 2. Keyboard scanner (keyboard.h)

Direct VIA/AY hardware scan — no ROM calls.  Include `keyboard.h`.

### Key codes

| Constant | Value | Key |
|---|---|---|
| `KEY_NONE` | `0x00` | No key |
| `KEY_ENTER` | `0x0D` | Return |
| `KEY_ESC` | `0x1B` | Escape |
| `KEY_DEL` | `0x7F` | Delete / Backspace |
| `KEY_UP` | `0x0B` | Cursor up |
| `KEY_DOWN` | `0x0A` | Cursor down |
| `KEY_LEFT` | `0x08` | Cursor left |
| `KEY_RIGHT` | `0x09` | Cursor right |
| `KEY_SPACE` | `0x20` | Space |
| `KEY_F1`–`KEY_F9` | `0xB1`–`0xB9` | FUNCT + 1–9 |
| `KEY_F10` | `0xB0` | FUNCT + 0 |

CTRL + letter produces `letter & 0x1F`. Constants `KEY_CTRL_A` (1),
`KEY_CTRL_C` (3), `KEY_CTRL_X` (24), `KEY_CTRL_Z` (26) are provided.

`KEY_TAB` (`0x09`) is the same value as `KEY_RIGHT` — context-dependent.

### Modifier flags (keyb_modifiers)

| Constant | Bit | Meaning |
|---|---|---|
| `MOD_SHIFT` | `0x01` | Shift held |
| `MOD_CTRL` | `0x02` | Ctrl held |
| `MOD_FUNCT` | `0x04` | FUNCT held (Atmos) |
| `MOD_CAPSLOCK` | `0x08` | Caps Lock active |

`keyb_modifiers` is written by `keyb_poll()`.

### Global state

```c
extern uint8_t  keyb_matrix[8];   // raw 8×8 matrix bits
extern volatile uint8_t keyb_char;    // last decoded key
extern uint8_t  keyb_modifiers;   // current modifier flags
```

### Functions

```c
void    keyb_scan(void);
```
Scan the full 8×8 matrix into `keyb_matrix[]`.  Takes ~1–2 ms at 1 MHz.
Call this from a polling loop or timer IRQ.

```c
uint8_t keyb_decode(void);
```
Decode `keyb_matrix[]` into `keyb_char` and `keyb_modifiers`.  Returns the
decoded ASCII/key code (0 = no key).  Handles Shift, Ctrl, FUNCT, Caps Lock.

```c
uint8_t keyb_poll(void);
```
Scan + decode + handle key repeat.  Updates `keyb_char`.  Returns the key
code (0 = no key).  Repeat fires after ~400 ms, then every ~100 ms.
**Use this as the main polling function.**

```c
uint8_t keyb_getch(void);
```
Blocking: loops `keyb_poll()` until a key is pressed.  Returns the key code.

```c
uint8_t keyb_check(void);
```
Non-blocking check.  Returns non-zero if any key is currently held.

### Animation delay without sleep

`keyb_scan()` takes ~370 cycles at 1 MHz.  Use it as a delay unit:

```c
for (uint8_t d = 0; d < 80; d++) keyb_scan();   // ≈ 30 ms
```

Hardware register accesses in `keyb_scan` prevent the Oscar64 optimiser from
collapsing the loop.

---

## 3. Character window library (charwin.h)

A character-based windowed display library for the Oric 40×28 text screen.
All functions use direct screen RAM writes — no ROM calls.

Include `charwin.h`.  The library auto-compiles `charwin.c` via
`#pragma compile`.

### Window model

Each window is described by an `OricCharWin` struct:

```c
typedef struct {
    uint8_t sx, sy;   // top-left corner (sx >= 2)
    uint8_t wx, wy;   // width and height in characters
    uint8_t cx, cy;   // cursor position within the window (0-based)
    uint8_t ink;      // ink colour (A_FW* constant)
    uint8_t paper;    // paper colour (A_BG* constant)
} OricCharWin;
```

**Column layout:** `cwin_clear` writes the window's ink attribute at screen
column 0 and paper attribute at screen column 1 for every managed row.
Content occupies columns `sx` through `sx + wx - 1`.  The minimum `sx` is 2
to leave room for the two attribute columns.

### Initialisation

```c
void charwin_init(void);
```
Build the row address lookup table.  **Call once before any `cwin_*` function.**

```c
void cwin_init(OricCharWin *w,
               uint8_t sx, uint8_t sy,
               uint8_t wx, uint8_t wy,
               uint8_t ink, uint8_t paper);
```
Populate a window struct.  Enforces `sx >= 2`.  Does not draw anything.

### Clear and fill

```c
void cwin_clear(OricCharWin *w);
```
Fill content with spaces (`0x20`).  Write ink attribute at column 0 and paper
attribute at column 1 for every row in the window.  Reset cursor to (0, 0).

```c
void cwin_fill_rect(OricCharWin *w,
                    uint8_t x, uint8_t y,
                    uint8_t bw, uint8_t bh,
                    uint8_t ch);
```
Fill a `bw × bh` rectangle at window-relative `(x, y)` with character `ch`.
Clips to window bounds.

### Positional writes (no cursor update)

```c
void cwin_putat_char(OricCharWin *w, uint8_t x, uint8_t y, uint8_t ch);
```
Write one character at window-relative `(x, y)`.

```c
uint8_t cwin_getat_char(OricCharWin *w, uint8_t x, uint8_t y);
```
Read the character at window-relative `(x, y)` from screen RAM.

```c
void cwin_putat_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s);
```
Write a null-terminated string starting at `(x, y)`.  Clips at the right
edge.  Attribute bytes embedded with `ASTR_*` macros are written as-is
(each occupies one column and shifts subsequent text right).

```c
void cwin_putat_dblhi_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s);
```
Write `s` in double-height at `(x, y)`.  Outputs `A_STD2H`, the string, then
`A_STD` on **both** row `y` and row `y+1` in a single call (the hardware
produces top-half on `y`, bottom-half on `y+1`).  Requires `y + 1 < w->wy`.

```c
void cwin_putat_printf(OricCharWin *w, uint8_t x, uint8_t y,
                       const char *fmt, ...);
```
Printf-style write at `(x, y)`.  Clips at right edge.  Supported specifiers:
`%d` (int16), `%u` (uint16), `%x` (uint16 hex uppercase), `%s`, `%c`, `%%`,
with width and zero-fill (e.g. `%04u`).  No float support (`-dNOFLOAT`).

### Cursor-advancing writes

```c
void cwin_put_char(OricCharWin *w, uint8_t ch);
void cwin_put_string(OricCharWin *w, const char *s);
void cwin_put_attr(OricCharWin *w, uint8_t attr);
void cwin_printf(OricCharWin *w, const char *fmt, ...);
```
Write at the current cursor position and advance the cursor.  No newline
or scroll handling.  `cwin_put_attr` is the correct way to write `A_FWBLACK`
(0x00) since it cannot appear in a C string literal.

### Console mode (newline + scroll)

```c
void cwin_console_put_char(OricCharWin *w, uint8_t ch);
void cwin_console_put_string(OricCharWin *w, const char *s);
```
Write in console mode: `\n` advances the cursor to the start of the next row,
scrolling the window if the cursor is already on the last row.  Other
characters wrap at the right edge.

**Caution:** `ASTR_CHARSET_STD2H` (`\x0A`) equals `\n` and will trigger a
scroll.  Use `cwin_put_attr` to write it in console mode.

### Scrolling

```c
void cwin_scroll_up(OricCharWin *w);
```
Scroll window content up by one row.  The new bottom row is filled with
spaces and its attributes are refreshed.

```c
void cwin_scroll_down(OricCharWin *w);
```
Scroll window content down by one row.  The new top row is filled with spaces
and its attributes are refreshed.

### Insert and delete

```c
void cwin_insert_char(OricCharWin *w);
```
Insert a space at the cursor position, shifting the rest of the row right.
The character at column `wx - 1` is discarded.  Cursor is not moved.

```c
void cwin_delete_char(OricCharWin *w);
```
Delete the character at the cursor position, shifting the rest of the row
left.  Column `wx - 1` is filled with a space.  Cursor is not moved.

### Print line

```c
void cwin_printline(OricCharWin *w, const char *s);
```
Write `s` at the current cursor position, then advance the cursor to the
start of the next row.  Scrolls if on the last row.  Use with
`cwin_putat_printf` to output numbered lines that auto-scroll:

```c
uint8_t cy = w.cy;   // temp variable — avoids Oscar64 cast-before-member gotcha
cwin_putat_printf(&w, 0, cy, "Line %u", (uint16_t)i);
cwin_printline(&w, "");
```

### Cursor

```c
void cwin_cursor_show(OricCharWin *w, bool on);
```
Show (`on = true`) or hide the cursor at the current position using
inverse-video toggle.  The caller must track the show/hide state to avoid
double-toggling.

```c
bool cwin_cursor_left(OricCharWin *w);
bool cwin_cursor_right(OricCharWin *w);
bool cwin_cursor_up(OricCharWin *w);
bool cwin_cursor_down(OricCharWin *w);
```
Move the cursor one position.  Return `true` if moved, `false` if already at
the window edge.

### Text input widget

```c
signed int cwin_textinput(OricCharWin *w,
                          uint8_t x, uint8_t y,
                          uint8_t vwidth,
                          char *str, uint8_t maxlen,
                          uint8_t validation);
```
Interactive single-line text input at window-relative `(x, y)`.

| Parameter | Meaning |
|---|---|
| `vwidth` | Visible viewport width; `vwidth < maxlen` enables horizontal scrolling |
| `str` | Pre-initialised buffer of at least `maxlen + 1` bytes |
| `maxlen` | Maximum string length (not counting NUL) |
| `validation` | Input filter: `VINPUT_ALL` (0), `VINPUT_NUMS` (1), `VINPUT_ALPHA` (2), `VINPUT_WILD` (4) |

Returns the string length on ENTER, or `-1` on ESC.

Validation flags:

| Constant | Value | Allowed characters |
|---|---|---|
| `VINPUT_ALL` | `0` | All printable characters |
| `VINPUT_NUMS` | `1` | Digits 0–9 only |
| `VINPUT_ALPHA` | `2` | Letters + digits (adds `VINPUT_NUMS`) |
| `VINPUT_WILD` | `4` | Add `*` and `?` to any combination |

### Viewport — scrollable view into a source map

```c
typedef struct {
    uint8_t     *sourcebase;    // flat source character map
    uint16_t     sourcewidth;   // bytes per row in the map (>= win->wx)
    uint16_t     sourceheight;  // total rows in the map
    uint16_t     viewx;         // horizontal scroll offset
    uint16_t     viewy;         // vertical scroll offset
    OricCharWin *win;           // target display window
} OricViewport;
```

The source map is a flat byte array: `map[row * sourcewidth + col]`.  Only
character bytes are stored — attributes come from the window's `ink` and
`paper` settings.

**Important:** Large source maps must be declared `static`.  A 60 × 20 = 1200-byte
map allocated as a local variable would overflow the 256-byte 6502 stack.

```c
void cwin_viewport_init(OricViewport *vp,
                        uint8_t *sourcebase,
                        uint16_t sourcewidth, uint16_t sourceheight,
                        OricCharWin *win);
```
Initialise the viewport.  Sets `viewx = viewy = 0`.  Does not draw.

```c
void cwin_viewport_blit(OricViewport *vp);
```
Copy the current view region to the display window, including ink/paper
attributes on every row.

```c
void cwin_viewport_scroll(OricViewport *vp, uint8_t dir);
```
Scroll one unit in direction `dir` (`KEY_UP`, `KEY_DOWN`, `KEY_LEFT`,
`KEY_RIGHT`).  Clamps to source bounds, then calls `cwin_viewport_blit`.

Example interactive scroll loop:

```c
static uint8_t map[20 * 60];   // static — 1200 bytes, must not be on the stack
// ... fill map ...
OricCharWin vpwin;
OricViewport vp;
cwin_init(&vpwin, 2, 3, 34, 10, A_FWWHITE, A_BGBLUE);
cwin_viewport_init(&vp, map, 60, 20, &vpwin);
cwin_viewport_blit(&vp);

uint8_t k;
while ((k = keyb_poll()) != KEY_ESC) {
    if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT)
        cwin_viewport_scroll(&vp, k);
}
```

> **OSE note:** the 40×27 canvas (`src/canvas.c`) deliberately does **not**
> use `OricViewport`/`cwin_viewport_blit` — see `ARCHITECTURE.md` §2.6 for
> why (it would clobber arbitrary serial-attribute bytes the user places in
> the canvas). `OricViewport` remains correct for any future scrollable
> sub-view (e.g. a file browser).

### Overlay RAM save/restore

```c
void cwin_push(OricCharWin *w);
void cwin_pop(OricCharWin *w);
```
Save (`push`) and restore (`pop`) the rows covered by window `w` using
overlay RAM.  Up to 8 levels (LIFO stack).  **Requires LOCI device** —
not testable in Oricutron; skip gracefully when LOCI is absent.

> **OSE note:** OSE's menu system (§7) does **not** use `cwin_push`/
> `cwin_pop` — it has its own main-RAM window-save stack
> (`menu_winsave`/`menu_winrestore`) so popups close cleanly in plain
> Oricutron without LOCI. `cwin_push`/`cwin_pop` remain available from
> `charwin.h` for any future overlay-RAM use case.

### Key read

```c
uint8_t cwin_getch(void);
```
Blocking key read (delegates to `keyb_getch`).

---

## 4. IJK joystick (ijk.h)

> **Present in `include/`, not yet wired into OSE.** `ijk.h/c` were copied
> verbatim from locifilemanager-v2 for a later input-method phase; no file in
> `src/` currently `#include`s `ijk.h`, so this code is not compiled into
> `build/oseloci*.tap` yet (see `ARCHITECTURE.md` §2.4). Documented here for
> when it is wired in.

Raxiss IJK interface using VIA Port A (`$030F`) and Port B bit 4.
Include `ijk.h` separately from `charwin.h` — it is not a dependency of
the window library.

The IJK interface shares VIA Port A with the keyboard scanner.  `ijk_read`
brackets all Port A accesses with SEI/CLI to prevent conflict.

### Direction bit masks

| Constant | Value | Direction |
|---|---|---|
| `IJK_JOY_RIGHT` | `0x01` | Right |
| `IJK_JOY_LEFT` | `0x02` | Left |
| `IJK_JOY_FIRE` | `0x04` | Fire button |
| `IJK_JOY_DOWN` | `0x08` | Down |
| `IJK_JOY_UP` | `0x10` | Up |

A bit is **1** when the direction is pressed (active-high after inversion).

### Global state

```c
extern uint8_t ijk_present;  // non-zero if IJK detected
extern uint8_t ijk_ljoy;     // left joystick state
extern uint8_t ijk_rjoy;     // right joystick state
```

### Functions

```c
void ijk_detect(void);
```
Probe the hardware for an IJK interface.  Sets `ijk_present` to non-zero if
found.  Call once after startup (or delegate to `menu_init` which calls it
in locifilemanager-v2 — OSE's `menu_init`, §7, does not).

```c
void ijk_read(void);
```
Read both joysticks into `ijk_ljoy` and `ijk_rjoy`.  No-op if `ijk_present`
is zero.

### Typical use in a menu/input loop

```c
ijk_detect();
// ...
uint8_t k = keyb_poll();
if (ijk_present && k == KEY_NONE) {
    ijk_read();
    if      (ijk_ljoy & IJK_JOY_UP)    k = KEY_UP;
    else if (ijk_ljoy & IJK_JOY_DOWN)  k = KEY_DOWN;
    else if (ijk_ljoy & IJK_JOY_LEFT)  k = KEY_LEFT;
    else if (ijk_ljoy & IJK_JOY_RIGHT) k = KEY_RIGHT;
    else if (ijk_ljoy & IJK_JOY_FIRE)  k = KEY_ENTER;
}
```

---

## 5. LOCI mass-storage API (loci.h)

> **Present in `include/`, not yet wired into OSE.** `loci.h/c` were copied
> verbatim from locifilemanager-v2 for the planned LOCI file-I/O phase; no
> file in `src/` currently `#include`s `loci.h`, so this code is not compiled
> into `build/oseloci*.tap` yet (see `ARCHITECTURE.md` §2.4). The File menu's
> Save/Load Screen/Project/Combined items (§7's pulldown index 1) currently
> show a "not yet implemented" popup pending this phase. Documented here for
> when it is wired in.

High-level C API for the LOCI mass-storage device.  Include `loci.h`.

> **All LOCI operations require real LOCI hardware.**  Oricutron does not
> emulate the MIA or TAP registers.  `loci_present()` returns `false` in
> the emulator; all file/directory/mount operations degrade gracefully.

### Hardware registers

#### MIA — Mass Interface Adapter at `$03A0`

```c
#define MIA (*(volatile struct __LOCI_MIA *)0x03A0)
```

Key fields used by high-level functions:

| Field | Address | Purpose |
|---|---|---|
| `MIA.ready` | `$03A0` | RX/TX ready bits (`MIA_READY_TX`, `MIA_READY_RX`) |
| `MIA.tx` | `$03A1` | Transmit byte to firmware |
| `MIA.rx` | `$03A2` | Receive byte from firmware |
| `MIA.xstack` | `$03AC` | XSTACK — parameter/result byte exchange |
| `MIA.errno_lo` | `$03AD` | Error code low byte |
| `MIA.op` | `$03AF` | Write an operation code to invoke it |
| `MIA.busy` | `$03B2` | Bit 7 set while operation is in progress |
| `MIA.areg` | `$03B4` | A register (argument/result low byte) |
| `MIA.xreg` | `$03B6` | X register (argument/result high byte) |

#### TAP — Tape controller at `$0315`

```c
#define TAP (*(volatile struct __LOCI_TAP *)0x0315)
```

| Field | Address | Purpose |
|---|---|---|
| `TAP.cmd` | `$0315` | Command (`TAP_CMD_PLAY/REC/REW/BIT/FFW`) |
| `TAP.status` | `$0316` | Status |
| `TAP.data` | `$0317` | Data |

### Global state

```c
extern uint8_t loci_errno;  // error code from last failed operation
extern LociCfg locicfg;     // device configuration (filled by get_locicfg)

#define LOCI_EACCES 3        // "not empty": loci_errno set by MIA_OP_UNLINK
                             // on a non-empty directory (FatFs FR_DENIED /
                             // POSIX ENOTEMPTY; confirmed against
                             // Phosphoric's loci_fs.c op_unlink, revisit
                             // against real LOCI firmware if it differs)
```

### Data structures

```c
typedef struct { uint8_t major, minor, patch; } LociVersion;
```

```c
typedef struct {
    uint8_t     devnr;
    uint8_t     validdev[10];
    LociVersion version;
    LociUname   uname;
} LociCfg;
```

```c
typedef struct {
    int16_t  fd;
    uint16_t off;
    char     name[64];
} LociDir;   // directory stream handle
```

```c
typedef struct {
    int16_t  d_fd;
    char     d_name[64];
    uint8_t  d_attrib;     // DIR_ATTR_RDO, DIR_ATTR_SYS, DIR_ATTR_DIR
    uint8_t  reserved;
    uint32_t d_size;
} LociDirent;   // 72 bytes
```

Directory attribute flags: `DIR_ATTR_RDO` (0x01 = read-only),
`DIR_ATTR_SYS` (0x04 = system), `DIR_ATTR_DIR` (0x10 = directory).

```c
typedef struct {
    uint8_t flag_int, flag_str, type, autorun;
    uint8_t end_addr_hi, end_addr_lo;
    uint8_t start_addr_hi, start_addr_lo;
    uint8_t reserved;
    uint8_t filename[16];
} LociTapHdr;   // 25-byte Oric tape header
```

File open flags: `O_RDONLY` (1) · `O_WRONLY` (2) · `O_RDWR` (3) ·
`O_CREAT` (0x10) · `O_TRUNC` (0x20) · `O_APPEND` (0x40) · `O_EXCL` (0x80).

Seek whence: `SEEK_CUR` (0) · `SEEK_END` (1) · `SEEK_SET` (2).

### Detection and configuration

```c
bool loci_present(void);
```
Return `true` if a LOCI device is active.  Checks for the `'L'` signature at
`$0319`.  **Call before any LOCI operation.**

```c
void get_locicfg(void);
```
Populate `locicfg`: device count, firmware version, system information via
`MIA_OP_UNAME`.

```c
bool loci_check_fw(uint8_t major, uint8_t minor, uint8_t patch);
```
Return `true` if the firmware version is ≥ `major.minor.patch`.  Suitable
for version-gating features.

```c
const char *get_loci_devname(uint8_t devid, uint8_t maxlength);
```
Return the drive label string for device `devid`.

```c
void loci_uname(LociUname *buf);
```
Populate a `LociUname` struct via XSTACK.

### System utilities

```c
int16_t phi2(void);
```
Return the CPU clock frequency in kHz (typically 1000 for Oric Atmos).

```c
int32_t loci_lrand(void);
```
Return a random 32-bit integer from the LOCI firmware RNG.

### Overlay RAM helpers

```c
void enable_overlay_ram(void);
void disable_overlay_ram(void);
```
Enable/disable the overlay RAM by writing to `MICRODISCCFG` at `$0314`.
Always disable before returning to normal ROM execution.  Used by the menu
system and `cwin_push`/`cwin_pop` in locifilemanager-v2 — currently unused in
OSE (§3/§7 notes).

### XRAM access

XRAM is extended RAM accessible only via MIA DMA channels.  Base address
for the copy buffer: `COPYBUF_XRAM_ADDR` (`0x8000`), size `COPYBUF_XRAM_SIZE`
(`0x0800` bytes).

```c
void    xram_poke(uint16_t addr, uint8_t val);
uint8_t xram_peek(uint16_t addr);
void    xram_memcpy_to(uint16_t dest, const void *src, uint16_t count);
void    xram_memcpy_from(void *dest, uint16_t src, uint16_t count);
```

### File I/O

```c
int16_t loci_open(const char *path, uint16_t flags);
```
Open a file.  Returns a file descriptor (≥ 0) on success, negative on error.
`flags` is a combination of `O_*` constants.

```c
int16_t loci_close(int16_t fd);
int16_t loci_read(int16_t fd, void *buf, uint16_t count);
int16_t loci_write(int16_t fd, const void *buf, uint16_t count);
int32_t loci_lseek(int16_t fd, int32_t offset, uint8_t whence);
int16_t loci_unlink(const char *path);   // delete file
int16_t loci_rename(const char *oldpath, const char *newpath);
```

On error, these functions set `loci_errno` and return a negative value.

### High-level file operations

```c
bool file_exists(const char *path);
```
Return `true` if the file at `path` exists.

```c
int16_t file_load(const char *path, void *dst, uint16_t count);
```
Open, read `count` bytes into `dst`, close.  Returns bytes read or negative.

```c
int16_t file_save(const char *path, const void *src, uint16_t count);
```
Create/overwrite, write `count` bytes from `src`, close.  Returns bytes
written or negative.

```c
int16_t file_copy(const char *dst, const char *src);
```
Copy file `src` to `dst` using the XRAM copy buffer.  Returns bytes copied
or negative on error.

```c
int16_t file_copy_progress(const char *dst, const char *src,
                            uint8_t progx, uint8_t progy, uint8_t progl);
```
Like `file_copy`, but draws a progress bar directly into screen RAM at
column `progx`, row `progy` (`progl` cells wide) while copying via the XRAM
buffer. Polls `keyb_check()` once per chunk; if `KEY_ESC` is pressed,
copying stops immediately, the partially written `dst` file is removed via
`loci_unlink`, and the function returns `-2`. Returns `0` on success, or
another negative `loci_errno`-setting error code on I/O failure.

### Directory operations

```c
LociDir    *loci_opendir(const char *path);
void        loci_closedir(LociDir *dir);
LociDirent *loci_readdir(LociDir *dir);
int16_t     loci_mkdir(const char *path);
void        loci_getcwd(char *buf, uint8_t len);
```

`loci_readdir` returns a pointer to a static `LociDirent` buffer, or `NULL`
at end-of-directory.  The buffer is overwritten on each call.

`loci_getcwd` fills `buf` (size `len`) with the current working directory
path.

### Mount operations

```c
int16_t loci_mount(int16_t drive, const char *path, const char *filename);
int16_t loci_umount(int16_t drive);
```

Mount/unmount a disk, tape, or ROM image on drive `drive` (0-based).

### Tape operations

```c
int32_t tap_seek(int32_t pos);
int32_t tap_tell(void);
int32_t tap_read_header(LociTapHdr *hdr);
```

`tap_seek` / `tap_tell` position the virtual tape.  `tap_read_header` reads
the 25-byte Oric tape header at the current position into `*hdr`.

---

## 6. Generic charset library (include/charset.h)

New in OSE — not present in locifilemanager-v2. Pure addressing/copy helpers
for the Oric Atmos charset-RAM model (`CHARSET_STD`/`CHARSET_ALT`/
`CHARSETROM`, see §1) — no application state, reusable across any Oric Atmos
project that redefines charset RAM at runtime. Include `charset.h`; the
library auto-compiles `charset.c` via `#pragma compile`.

### Constants

| Constant | Value | Meaning |
|---|---|---|
| `CHARSET_GLYPH_AREA_OFFSET` | `0x100` | Offset of the displayable range (codes `0x20-0x7F`) within a `CHARSET_STD`/`CHARSET_ALT` bank |
| `CHARSET_GLYPH_AREA_SIZE` | `768` | Size of the displayable range (96 glyphs × 8 bytes) |
| `CHARSET_GLYPH_BYTES` | `8` | Bytes per glyph (one per pixel row) |
| `CHARSET_GLYPH_WIDTH` | `6` | Visible pixel columns per row (bits 5..0, bit5 = leftmost) |

### Addressing and copy primitives

```c
uint16_t charset_address(uint8_t screencode, uint8_t altorstd);
```
Compute the charset-RAM address of `screencode`'s 8-byte glyph
(`base + screencode*8`, codes `0x00-0x7F`). `altorstd`: `0` = `CHARSET_STD`,
`1` = `CHARSET_ALT`.

```c
void charset_save(uint16_t base, uint8_t *dest);
```
Copy the displayable glyph range (`CHARSET_GLYPH_AREA_SIZE` bytes) from
charset bank `base` (`CHARSET_STD` or `CHARSET_ALT`) into `dest[]`.

```c
void charset_load(uint16_t base, const uint8_t *src);
```
Copy `CHARSET_GLYPH_AREA_SIZE` bytes from `src[]` into charset bank `base`'s
displayable glyph range. `src` may be another charset bank or `CHARSETROM`.

```c
const uint8_t *charset_rom_glyph(uint8_t screencode);
```
Pointer to `screencode`'s 8-byte glyph in `CHARSETROM` (codes `0x20-0x7F`
only — addressed as `CHARSETROM + (screencode-0x20)*8`, the **opposite**
offset convention from `charset_address()`).

### Glyph-bitmap operations

In-place transforms on an 8-byte Oric glyph (`CHARSET_GLYPH_BYTES` rows ×
`CHARSET_GLYPH_WIDTH` visible bits/row, bit5 = leftmost). `glyph` is
typically a pointer into live charset RAM (see `charset_address()`), hence
`volatile`.

```c
void charset_glyph_invert(volatile uint8_t *glyph);
```
Invert all pixels (XOR each row with the 6-bit mask `0x3F`).

```c
void charset_glyph_mirror_v(volatile uint8_t *glyph);
```
Mirror vertically: reverse the order of the 8 rows.

```c
void charset_glyph_mirror_h(volatile uint8_t *glyph);
```
Mirror horizontally: reverse the bit order of each row.

```c
void charset_glyph_scroll_up(volatile uint8_t *glyph);
```
Scroll all rows up by one, wrapping row 0 to the bottom.

```c
void charset_glyph_scroll_down(volatile uint8_t *glyph);
```
Scroll all rows down by one, wrapping the bottom row to row 0.

```c
void charset_glyph_rotate_left(volatile uint8_t *glyph);
```
Rotate each row's bits left by one (bit5 wraps to bit0).

```c
void charset_glyph_rotate_right(volatile uint8_t *glyph);
```
Rotate each row's bits right by one (bit0 wraps to bit5).

### Callers

`src/charsetedit.c` (character editor — SPACE/DEL toggle the pixel under the
cursor; `i`/`x`/`y`/`u`/`d`/`l`/`r` invoke
`invert`/`mirror_h`/`mirror_v`/`scroll_up`/`scroll_down`/`rotate_left`/
`rotate_right`; `s` restores the current glyph via `charset_rom_glyph` +
`memcpy`) and `src/charsetswap.c` (below) are the only current callers.

### Charset-swap mechanism (src/charsetswap.h)

App-specific glue built on top of `charset_save`/`charset_load`/
`charset_rom_glyph`, used by the menu system (§7) so popup chrome always
renders with readable, ROM-standard glyphs even after the user has redefined
the standard charset:

```c
void charsetswap_mark_changed(void);  // call once when the user edits a glyph
void charsetswap_enter(void);         // opt-in: back up + restore ROM-standard CHARSET_STD
void charsetswap_exit(void);          // opt-in: restore the user's CHARSET_STD from the backup
```

- **`charset_changed`-gated**: if the user never edited a glyph,
  `charsetswap_enter`/`exit` are no-ops.
- **Depth-counted**: nested popups (e.g. a pulldown opening an `areyousure`
  popup) only back up/restore once, on the outermost enter/exit pair.
- **Std-only**: `CHARSET_ALT` is *not* swapped (a spike confirmed `jsr
  $F816`/`ROM_ALTCHARS` is a no-op for `CHARSET_ALT` in this bare-metal
  context — see `CLAUDE.md`'s charset-swap section for details). `CHARSET_ALT`
  keeps the user's edits visible during popups.
- The character editor (`charsetedit_run()`) opts **out** of the swap (via
  `menu_winsave`'s `swap_charset=0`, §7) so live glyph edits stay visible
  while editing.

---

## 7. Menu system (menu.h)

Full-screen pulldown menu system: menu bar → pulldown menus → popup dialogs,
with a LIFO main-RAM window-save stack. Include `menu.h`. Adapted from
locifilemanager-v2's [`menu.h`](https://github.com/xahmol/locifilemanager-v2/blob/main/src/menu.h)/`menu.c` — **OSE's version differs
substantially**: a 4-item bar instead of 6, 5 pulldowns instead of 11, and
window save/restore copies to a **main-RAM buffer** instead of overlay RAM
(no LOCI required — menus close with no residue in plain Oricutron).

### Architecture

Three layers:
- **Menu bar** (row 0) — black ink, green paper, 4 titled items
  (Screen/File/Charset/Information).
- **Pulldown menus** — cyan items with yellow highlight; open below the bar
  item (top-menu pulldowns) or inside a popup (sub-menus, e.g. Yes/No).
- **Popup dialogs** — white background (`menu_wininit`); use the Yes/No
  pulldown (`menu_areyousure`) or a message + keypress (`menu_messagepopup`).

### Capacity constants

| Constant | Value | Meaning |
|---|---|---|
| `MENUBAR_MAXOPTIONS` | 4 | Number of bar items |
| `MENUBAR_MAXLENGTH` | 12 | Max chars per bar title (11 visible + NUL) |
| `PULLDOWN_NUMBER` | 5 | Total pulldown menus (0–4) |
| `PULLDOWN_MAXOPTIONS` | 6 | Max items per pulldown |
| `PULLDOWN_MAXLENGTH` | 17 | 16 visible chars + NUL |
| `MENU_WIN_DEPTH` | 9 | Max nested window saves |
| `MENU_WINBUF_SIZE` | 2048 | Main-RAM window-save buffer size (bytes) |

### Return codes

| Constant | Value | Meaning |
|---|---|---|
| `MENU_CANCEL` | 0 | ESC pressed (escapable pulldown) |
| `MENU_LEFT_ARROW` | 18 | Left arrow → caller opens the previous bar item |
| `MENU_RIGHT_ARROW` | 19 | Right arrow → caller opens the next bar item |
| `MENU_YESNO` | 4 | Index of the Yes/No pulldown |

### Pulldown index map

| Index | Menu | Items (`pulldown_options[]`) |
|---|---|---|
| 0 | Screen | 4 — Width/Height (live), Clear, Fill |
| 1 | File | 6 — Save/Load Screen, Save/Load Project, Save/Load Combined |
| 2 | Charset | 6 — Load/Save Standard, Load/Save Alternate, Load/Save Combined |
| 3 | Information | 2 — Version/credits, Exit |
| 4 | Yes/No | 2 — Yes, No |

`pulldown_options[PULLDOWN_NUMBER] = { 4, 6, 6, 2, 2 }`.

`pulldown_titles[0][0]`/`[0][1]` (`"Width:%3u"`/`"Height:%3u"`) are
placeholders, rewritten at runtime by `src/menudata.c`'s
`update_size_titles()` whenever the canvas size changes. The File/Charset
items (indices 1–2, and Information item 1 = "Version/credits") currently
show a "not yet implemented" popup (`menu_messagepopup(MSG_MENU_NOTIMPL)`)
pending the LOCI file-I/O phase (§5).

### Data structures

```c
typedef struct {
    uint16_t  offset;       // offset into menu_winbuf[] where rows are saved
    uint8_t   ypos;         // first screen row saved
    uint8_t   height;       // number of rows saved (each row = SCREEN_COLS bytes)
    uint8_t   swap_charset; // 1 = charsetswap_enter() was called for this save
} MenuWinRecord;

typedef struct {
    char     titles[MENUBAR_MAXOPTIONS][MENUBAR_MAXLENGTH];
    uint8_t  xstart[MENUBAR_MAXOPTIONS];   // screen col of highlight attribute byte
    uint8_t  ypos;                          // screen row where bar is drawn
} MenuBar;
```

### Exported global data

```c
extern MenuBar menubar;
extern char    pulldown_options[PULLDOWN_NUMBER];
extern char    pulldown_titles[PULLDOWN_NUMBER][PULLDOWN_MAXOPTIONS][PULLDOWN_MAXLENGTH];
```

All defined in `src/menudata.c`, which also owns `menudata_init()` and
`menu_run()` — the app-specific table initialisation and dispatch loop, kept
separate so `menu.c` stays a generic, app-agnostic menu engine.

### Initialisation

```c
void menu_init(void);
```
Reset the window-save stack (`menu_win_depth = menu_win_ptr = 0`). Call once
at startup. (Unlike locifilemanager-v2's `menu_init`, this does **not** touch
`menubar`/`pulldown_titles` — those are static initialisers in
`menudata.c` — and does not call `ijk_detect()`, §4.)

### Drawing

```c
void menu_placebar(uint8_t y);
```
Draw the menu bar at screen row `y`: black ink / green paper across the row,
then each `menubar.titles[]` entry left to right with a 1-column gap on each
side, computing `menubar.xstart[]` from the title lengths.

**Layout constraint:** `sum(strlen(titles[0..3])) + 7 <= 39`, i.e.
`sum(strlen(titles)) <= 32`, or the last item's highlight write overruns the
40-column row and visually merges with the previous item. See `CLAUDE.md`
"Localisation" for the FR-build menu-bar bug this constraint caused (and how
it was fixed) — **any new menu-bar title in either language must respect
this limit**.

### Window save/restore (main-RAM, with charset-swap integration)

```c
void menu_winsave(uint8_t ypos, uint8_t height, uint8_t swap_charset);
```
Push a record onto the LIFO window-save stack and copy `height` full
40-byte rows starting at screen row `ypos` from screen RAM into
`menu_winbuf[]`. No-op if the stack (`MENU_WIN_DEPTH`) is full or the buffer
(`MENU_WINBUF_SIZE`) is exhausted.

`swap_charset`: `1` = also call `charsetswap_enter()` (popup chrome opts in
to the ROM-standard charset, §6, so menu text stays readable even if the user
has redefined glyphs); `0` = opt out (the character editor, so live glyph
edits stay visible).

```c
void menu_winrestore(void);
```
Pop the most recent record and copy its saved rows back to screen RAM. If
its `swap_charset` was `1`, also calls `charsetswap_exit()` — pairing is
automatic via the LIFO stack, callers cannot get it out of sync. No-op if the
stack is empty.

```c
void menu_wininit(uint8_t ypos, uint8_t height);
```
Paint a white-paper popup background for rows `ypos..ypos+height-1` (col 5 =
`A_BGWHITE`, col 6 = `A_FWBLACK`, cols 7–39 = space). Cols 0–4 retain existing
content. Caller is responsible for `menu_winsave()`/`menu_winrestore()`.

**Note:** unlike locifilemanager-v2's overlay-RAM `cwin_push`/`cwin_pop`
(§3) and `MENU_OVERLAY_BASE`-based menu stack, OSE's
`menu_winbuf[MENU_WINBUF_SIZE]` is a plain main-RAM array — no LOCI/overlay
RAM required. Largest nested path so far (menu bar + top-level pulldown +
resize popup + its shrink-confirm + its Yes/No pulldown = 40+240+480+240+80 =
1080 bytes) fits comfortably within the 2048-byte buffer, leaving headroom
for later phases.

### Pulldown menus

```c
uint8_t menu_pulldown(uint8_t xpos, uint8_t ypos,
                      uint8_t menunumber, uint8_t escapable);
```
Open pulldown menu `menunumber` at screen position `(xpos, ypos)`, saving the
covered rows first (`menu_winsave(ypos, height, 1)`) and restoring them on
exit. Highlights the current item; `KEY_UP`/`KEY_DOWN` move the selection,
`KEY_ENTER` chooses. For top-menu pulldowns (`menunumber < MENUBAR_MAXOPTIONS`),
`KEY_LEFT`/`KEY_RIGHT` request the previous/next bar item.

| Parameter | Meaning |
|---|---|
| `xpos` | Column of the highlight-bar ink byte (item text starts at `xpos+2`) |
| `ypos` | Screen row of the first item |
| `menunumber` | Index into `pulldown_titles[]`/`pulldown_options[]` (0–4) |
| `escapable` | `1` = ESC cancels (`MENU_CANCEL`); `0` = user must choose |

Returns `1..pulldown_options[menunumber]` (chosen item), `MENU_CANCEL` (0),
or `MENU_LEFT_ARROW`/`MENU_RIGHT_ARROW` for top-menu pulldowns.

```c
uint8_t menu_main(void);
```
Run the menu bar navigation loop: `KEY_LEFT`/`KEY_RIGHT` move the bar
highlight, `KEY_ENTER` opens `menu_pulldown()` for the highlighted item. If
the pulldown returns `MENU_LEFT_ARROW`/`MENU_RIGHT_ARROW`, moves to the
adjacent bar item and reopens its pulldown. `KEY_ESC` at the bar level exits.

Returns `menubarchoice*10 + menuoptionchoice` (1..49 = a real choice), or
`menubarchoice*10 + 99` (≥ 99) on ESC. **Callers should treat any return
value ≥ 99 as "exit the menu".**

### Popup dialogs

```c
uint8_t menu_areyousure(const char *message);
```
`menu_winsave(8, 6, 1)` + `menu_wininit(8, 6)`, show `message` and
`MSG_MENU_AREYOUSURE` ("Are you sure?"), then `menu_pulldown(20, 11,
MENU_YESNO, 0)`. Returns `1` (Yes) or `2` (No). Used by the Screen menu's
shrink-confirm (resizing the canvas smaller).

```c
void menu_messagepopup(const char *message);
```
`menu_winsave(8, 6, 1)` + `menu_wininit(8, 6)`, show `message` and
`MSG_MENU_PRESSAKEY` ("Press a key to continue"), wait for any keypress, then
restore. Used for the "not yet implemented" stubs (`MSG_MENU_NOTIMPL`).

### Dropped vs. locifilemanager-v2

Not present in OSE's `menu.h`/`menu.c` — not needed until later phases:
`menu_placeheader`/`menu_placetop` (no separate green header row — OSE's row
0 *is* the menu bar), `menu_fileerrormessage`/`menu_confirm_file`/
`menu_option_select`/`menu_popup_*` (LOCI file-error/selection dialogs), IJK
joystick input in the pulldown loop (`menu_pulldown`/`menu_main` call
`cwin_getch()` directly), and the overlay-RAM partition layout (the
main-RAM `menu_winbuf[]` described above replaces it entirely).

---

## 8. Build notes

### Compiler

Oscar64 (`$OSCAR64_HOME/bin/oscar64`, default `~/oscar64/`). Standard flags
(from `Makefile`):

```
-n -tf=bin -rt=include/oric_crt.c -i=include -i=src -O2 -dNOFLOAT \
  -dVERSION_MAJOR=$(VERSION_MAJOR) -dVERSION_MINOR=$(VERSION_MINOR) \
  -dVERSION_PATCH=$(VERSION_PATCH) $(LANGFLAG)
```

- `-i=include -i=src`: OSE adds `-i=src` (locifilemanager-v2 only has
  `-i=include`) so `#pragma compile` chains can resolve app-specific `.c`
  files in `src/` (e.g. `menu.c`, `menudata.c`, `charsetedit.c`,
  `charsetswap.c`) — see "#pragma compile chain" below.
- `-dVERSION_MAJOR/MINOR/PATCH`: feed `MSG_SPLASH_BUILD_FMT` on the splash
  screen (`src/main.c`).
- `$(LANGFLAG)`: `-dLANG_FR` for `make LANG=FR`, empty for the default
  `make`/`make LANG=EN`. See "Localisation" below.

### Localisation

`make LANG=FR` (or `make all-langs`) builds `build/oseloci_fr.tap` alongside
`build/oseloci.tap` (`LANGSUFFIX=_fr`). See `CLAUDE.md` "Localisation" for the
full `strings.h`/`strings_en.h`/`strings_fr.h` gateway pattern, the
menu-bar-title length constraint (§7), and the FR-unaccented-characters
requirement.

### #pragma compile chain

Each library header ends with `#pragma compile("filename.c")`. Oscar64
resolves this path relative to the include search directories (`-i=include
-i=src`, in that order).

- **Library `.c` files** (generic, reusable across projects) live in
  `include/`: `oric_crt.c`, `crt_math.c`, `charwin.c`, `keyboard.c`,
  `charset.c`, `ijk.c`, `loci.c`.
- **App-specific `.c` files** (OSE-only) live in `src/`: `canvas.c`,
  `statusbar.c`, `editor.c`, `menu.c`, `menudata.c`, `charsetedit.c`,
  `charsetswap.c`.

### Known Oscar64 gotchas

| Symptom | Cause | Fix |
|---|---|---|
| `va_arg` crash | `va_arg` is broken in native mode (`-n`) | Do not use `<stdarg.h>` |
| `#if MACRO` fails when set via `-d` | `-d` defines have no value | Use `#ifdef MACRO` |
| Cast applied before member access | Oscar64 parses `(type)struct.member` wrong | Use a temp variable |
| Ternary `? ptr : 0` in pointer function | Parser issue | Use `if`/`return` |
| Large local array crashes | 6502 stack is only 256 bytes | Declare large arrays `static` |
| `snprintf` not found | Not in Oscar64 `<stdio.h>` | Use `sprintf` instead |
| `-O2` caller-save set under-counted, stray screen garbage on a specific call site | Whole-program register allocator miscounts live registers across a call | Force a larger save set with a dummy `sprintf` to unused scratch RAM (`0xA000`) — see `src/menu.c`'s `menu_draw_item()` and `oscar64manual.md` "`-O2` whole-program register allocator: caller-save set can be under-counted" |
