// charwin.h - Character window library for Oric Atmos bare-metal
//
// Oric screen model (from Oricutron ula.c, confirmed hardware):
//   Attribute check: (byte & 0x60) == 0  → serial attribute (displays as paper-color box)
//   Character: everything else (0x20-0x7F, 0xA0-0xFF)
//   INK attrs:   0x00-0x07 (A_FW* values)
//   PAPER attrs: 0x10-0x17 (A_BG* values)
//   Inverse char: byte | 0x80  (e.g. 0xA0 = inverse space = solid ink block)
//   ULA resets to white-ink/black-paper at start of EACH rasterline.
//
// Window convention: cols 0-1 of every managed row hold INK+PAPER attributes.
// Window content starts at sx (minimum 2).

#ifndef CHARWIN_H
#define CHARWIN_H

#include <stdint.h>
#include <stdbool.h>
#include "oric.h"
#include "keyboard.h"

// -------------------------------------------------------------------------
// Text input validation flags (match v1 locifilemanager textInput)
// -------------------------------------------------------------------------

#define VINPUT_ALL      0   // All printable chars
#define VINPUT_NUMS     1   // Digits 0-9 only
#define VINPUT_ALPHA    2   // Alpha + digits (adds VINPUT_NUMS)
#define VINPUT_WILD     4   // Also allow '*' and '?'

// -------------------------------------------------------------------------
// OricCharWin — window descriptor
// -------------------------------------------------------------------------

typedef struct {
    uint8_t sx, sy;     // start col (min 2), start row
    uint8_t wx, wy;     // width, height in characters
    uint8_t cx, cy;     // cursor within window (0-based)
    uint8_t ink;        // INK color (A_FW* = 0-7)
    uint8_t paper;      // PAPER color (A_BG* = 16-23)
} OricCharWin;

// -------------------------------------------------------------------------
// Initialisation
// -------------------------------------------------------------------------

// Build row address lookup table. Call once before any cwin_* function.
void charwin_init(void);

// Populate window struct. Enforces sx >= 2.
void cwin_init(OricCharWin *w,
               uint8_t sx, uint8_t sy,
               uint8_t wx, uint8_t wy,
               uint8_t ink, uint8_t paper);

// -------------------------------------------------------------------------
// Screen I/O
// -------------------------------------------------------------------------

// Clear window: write INK at col 0, PAPER at col 1 of every row; fill
// content cols with space (0x20). Reset cursor to (0,0).
void cwin_clear(OricCharWin *w);

// Like cwin_clear(), but also blanks columns 2..(sx-1) -- the gap between
// the attribute bytes and a window whose sx > 2 -- instead of leaving it
// as stale background content. OricScreenEditorLOCI addition (not in
// locifilemanager-v2's original charwin.c); see charwin.c for when to use
// this instead of cwin_clear().
void cwin_clear_full(OricCharWin *w);

// Write ch at window-relative (x, y). No cursor update.
void cwin_putat_char(OricCharWin *w, uint8_t x, uint8_t y, uint8_t ch);

// Read ch at window-relative (x, y).
uint8_t cwin_getat_char(OricCharWin *w, uint8_t x, uint8_t y);

// Write null-terminated string starting at (x, y). Clips at window right edge.
void cwin_putat_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s);

// Write string in double-height at (x, y).
// Outputs A_STD2H attr at col x, then s, then A_STD on BOTH row y and row y+1.
// The two rows produce the top and bottom halves of the double-height characters.
// Caller only provides the string once. Requires y+1 < w->wy.
void cwin_putat_dblhi_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s);

// Write ch at cursor, advance cursor. No newline/scroll.
void cwin_put_char(OricCharWin *w, uint8_t ch);

// Write string at cursor. No wrap/scroll.
void cwin_put_string(OricCharWin *w, const char *s);

// Write a serial attribute byte at cursor and advance. Use for A_FWBLACK (0x00)
// which cannot be embedded in a C string literal (NUL terminator).
void cwin_put_attr(OricCharWin *w, uint8_t attr);

// printf-style formatted write via console output (handles '\n', wraps, scrolls).
// Supports: %d (int16), %u (uint16), %x (uint16 hex uppercase), %s, %c, %%.
// No float formatting (matches -dNOFLOAT build). Max 79 formatted chars.
void cwin_printf(OricCharWin *w, const char *fmt, ...);

// printf-style formatted write at (x, y). Clips at window right edge.
void cwin_putat_printf(OricCharWin *w, uint8_t x, uint8_t y, const char *fmt, ...);

// Write ch as console output: '\n' advances row (scrolls if needed), other
// chars wrap at right edge.
void cwin_console_put_char(OricCharWin *w, uint8_t ch);

// Write string via console (handles '\n').
void cwin_console_put_string(OricCharWin *w, const char *s);

// Fill bw×bh rectangle at window-relative (x,y) with ch. Clips to window.
void cwin_fill_rect(OricCharWin *w,
                    uint8_t x, uint8_t y,
                    uint8_t bw, uint8_t bh,
                    uint8_t ch);

// Scroll window content up by 1 row. New bottom row filled with spaces +
// attrs refreshed for that row.
void cwin_scroll_up(OricCharWin *w);

// Scroll window content down by 1 row. New top row filled with spaces + attrs.
void cwin_scroll_down(OricCharWin *w);

// Insert a space at the cursor position, shifting content right within the row.
// Character at the right edge (wx-1) is lost. Cursor is not moved.
void cwin_insert_char(OricCharWin *w);

// Delete the character at the cursor position, shifting content left within the row.
// Right edge (wx-1) is filled with a space. Cursor is not moved.
void cwin_delete_char(OricCharWin *w);

// Write string at cursor then advance to the start of the next line (auto-scrolls).
void cwin_printline(OricCharWin *w, const char *s);

// -------------------------------------------------------------------------
// Cursor
// -------------------------------------------------------------------------

// Show (on=true) or hide cursor at current position using inverse-video toggle.
// Caller must track show/hide state to avoid double-toggling.
void cwin_cursor_show(OricCharWin *w, bool on);

// Move cursor one position. Return true if moved, false if already at edge.
bool cwin_cursor_left(OricCharWin *w);
bool cwin_cursor_right(OricCharWin *w);
bool cwin_cursor_up(OricCharWin *w);
bool cwin_cursor_down(OricCharWin *w);

// -------------------------------------------------------------------------
// Viewport — scrollable view into a flat source character buffer
// -------------------------------------------------------------------------

// Source map layout: sourcebase[row * sourcewidth + col], col = 0..sourcewidth-1.
// Only character bytes are stored — attrs come from the window's ink/paper settings.
// The viewport blits a win->wx × win->wy slice at (viewx, viewy) to the display window.

typedef struct {
    uint8_t     *sourcebase;    // pointer to flat source character map
    uint16_t     sourcewidth;   // bytes per row in the source map (>= win->wx)
    uint16_t     sourceheight;  // total rows in the source map
    uint16_t     viewx;         // current horizontal scroll offset (0-based)
    uint16_t     viewy;         // current vertical scroll offset (0-based)
    OricCharWin *win;           // target display window
} OricViewport;

// Initialise a viewport. Sets viewx = viewy = 0. Does not draw.
void cwin_viewport_init(OricViewport *vp,
                        uint8_t *sourcebase,
                        uint16_t sourcewidth, uint16_t sourceheight,
                        OricCharWin *win);

// Blit the current view to the display window (redraws all visible rows with attrs).
void cwin_viewport_blit(OricViewport *vp);

// Scroll the viewport by one unit in the given direction (KEY_UP/DOWN/LEFT/RIGHT),
// clamp to source bounds, then call cwin_viewport_blit.
void cwin_viewport_scroll(OricViewport *vp, uint8_t dir);

// -------------------------------------------------------------------------
// Cursor — extended movement
// -------------------------------------------------------------------------

// Move cursor to window-relative (cx, cy) directly.
void cwin_cursor_move(OricCharWin *w, uint8_t cx, uint8_t cy);

// Advance cursor forward one position; wraps to cx=0, cy++ at right edge.
// Returns false if already at last cell (bottom-right).
bool cwin_cursor_forward(OricCharWin *w);

// Retreat cursor one position; wraps to cx=wx-1, cy-- at left edge.
// Returns false if already at first cell (top-left).
bool cwin_cursor_backward(OricCharWin *w);

// Move cursor to start of next line (cx=0, cy++). Does NOT scroll.
// Returns false if already on last row.
bool cwin_cursor_newline(OricCharWin *w);

// -------------------------------------------------------------------------
// Multi-character bulk I/O
// -------------------------------------------------------------------------

// Write exactly num chars at cursor, advancing cursor. No wrap/scroll.
void cwin_put_chars(OricCharWin *w, const char *chars, uint8_t num);

// Write exactly num chars at window-relative (x, y). Clips at right edge.
void cwin_putat_chars(OricCharWin *w, uint8_t x, uint8_t y, const char *chars, uint8_t num);

// Read exactly num chars from window-relative (x, y) into buffer.
// Clips at right edge; buffer is NOT null-terminated.
void cwin_getat_chars(OricCharWin *w, uint8_t x, uint8_t y, char *chars, uint8_t num);

// -------------------------------------------------------------------------
// Rectangle copy (no colour RAM — chars only)
// -------------------------------------------------------------------------

// Copy a bw×bh rectangle of characters from the window at (x,y) into
// a flat row-major buffer (bw bytes per row, bh rows).
// Buffer must be at least bw * bh bytes.
void cwin_get_rect(OricCharWin *w, uint8_t x, uint8_t y,
                   uint8_t bw, uint8_t bh, char *chars);

// Write a flat row-major buffer of characters into the window at (x,y).
// bw bytes per row, bh rows. Does not touch attribute bytes.
void cwin_put_rect(OricCharWin *w, uint8_t x, uint8_t y,
                   uint8_t bw, uint8_t bh, const char *chars);

// -------------------------------------------------------------------------
// Word-wrap print
// -------------------------------------------------------------------------

// Print str into the window with word-wrap. Spaces are used as word
// delimiters. Words longer than wx are split across lines.
// Uses console mode (scrolls at bottom). Does not add a trailing newline.
void cwin_printwrap(OricCharWin *w, const char *str);

// -------------------------------------------------------------------------
// Horizontal scroll
// -------------------------------------------------------------------------

// Shift all window rows LEFT by `by` columns. Right edge fills with spaces.
// Attribute bytes (cols 0-1) are not touched.
void cwin_scroll_left(OricCharWin *w, uint8_t by);

// Shift all window rows RIGHT by `by` columns. Left edge fills with spaces.
// Attribute bytes (cols 0-1) are not touched.
void cwin_scroll_right(OricCharWin *w, uint8_t by);

// -------------------------------------------------------------------------
// Overlay RAM save/restore — REQUIRES LOCI device (not testable in Oricutron)
// -------------------------------------------------------------------------

// Push: copy full rows [sy, sy+wy) from screen RAM to overlay RAM (LIFO).
// Up to OVERLAY_STACK_DEPTH (8) levels.
void cwin_push(OricCharWin *w);

// Pop: restore last pushed rows from overlay RAM back to screen RAM.
void cwin_pop(OricCharWin *w);

// -------------------------------------------------------------------------
// Key input and text entry
// -------------------------------------------------------------------------

// Blocking key read (calls keyb_getch).
uint8_t cwin_getch(void);

// Text-input widget at window-relative (x, y).
//   vwidth:     visible viewport width (may be < maxlen; enables scrolling)
//   str:        pre-initialised buffer, at least maxlen+1 bytes
//   maxlen:     maximum string length (not counting null terminator)
//   validation: VINPUT_* flags (0 = all printable)
// Returns: string length on ENTER, -1 on ESC.
//
// Based on v1 locifilemanager textInput() by Xander Mol, adapted from
// DraBrowse 1.0e by Sascha Bader (2009).
signed int cwin_textinput(OricCharWin *w,
                          uint8_t x, uint8_t y,
                          uint8_t vwidth,
                          char *str, uint8_t maxlen,
                          uint8_t validation);

#pragma compile("charwin.c")

#endif  // CHARWIN_H
