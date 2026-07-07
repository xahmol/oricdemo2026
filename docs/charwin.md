# Character window library (charwin.h)

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

### Overlay RAM save/restore

```c
void cwin_push(OricCharWin *w);
void cwin_pop(OricCharWin *w);
```
Save (`push`) and restore (`pop`) the rows covered by window `w` using
overlay RAM.  Up to 8 levels (LIFO stack).  **Requires LOCI device** —
not testable in Oricutron; skip gracefully when LOCI is absent.

### Key read

```c
uint8_t cwin_getch(void);
```
Blocking key read (delegates to `keyb_getch`).
