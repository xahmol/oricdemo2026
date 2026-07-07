// charwin.c - Character window library for Oric Atmos bare-metal
//
// Screen model (confirmed from Oricutron ula.c):
//   Attribute: (byte & 0x60) == 0  → INK/PAPER/mode serial attribute
//   Character:  byte in 0x20-0x7F (normal) or 0xA0-0xFF (inverse)
//   ULA resets to white-ink/black-paper at start of each rasterline.
//   => Cols 0 and 1 of every managed row must hold INK and PAPER attrs.
//
// Based on v1 locifilemanager generic.c windowing by Xander Mol.

#include "oric.h"
#include "keyboard.h"
#include "charwin.h"

// -------------------------------------------------------------------------
// Row address lookup table — computed once in charwin_init()
// row_base[r] = TEXTVRAM + r * SCREEN_COLS
// -------------------------------------------------------------------------

static uint16_t row_base[SCREEN_ROWS];

// -------------------------------------------------------------------------
// Overlay RAM save stack (LIFO, LOCI-only)
// -------------------------------------------------------------------------

#define OVERLAY_STACK_DEPTH 8

typedef struct {
    uint8_t  sy, wy;
    uint16_t addr;
    uint16_t size;
} SaveRecord;

static SaveRecord save_stack[OVERLAY_STACK_DEPTH];
static uint8_t   save_depth;
static uint16_t  overlay_sp;

// -------------------------------------------------------------------------
// Init
// -------------------------------------------------------------------------

/**
 * Initialize the charwin library: build the row_base[] address lookup table
 * (row_base[r] = TEXTVRAM + r * SCREEN_COLS) and reset the overlay-RAM save
 * stack. Must be called once before any other cwin_* or charwin_* function.
 *
 * @return (none) -- populates row_base[], resets overlay_sp and save_depth.
 */
void charwin_init(void)
{
    // Use addition to fill row_base — avoids 16-bit multiply on 6502
    uint16_t addr = TEXTVRAM;
    for (uint8_t r = 0; r < SCREEN_ROWS; r++)
    {
        row_base[r] = addr;
        addr = (uint16_t)(addr + SCREEN_COLS);
    }
    overlay_sp = OVERLAY_BASE;
    save_depth = 0;
}

/**
 * Populate a window descriptor with its geometry and colors. Cursor is reset
 * to (0,0). Enforces sx >= 2 (columns 0-1 of every managed row hold the
 * INK/PAPER attribute bytes).
 *
 * @param w     Window descriptor to initialize.
 * @param sx    Start column (clamped to >= 2).
 * @param sy    Start row.
 * @param wx    Width in characters.
 * @param wy    Height in characters.
 * @param ink   INK attribute (A_FW* value, 0x00-0x07).
 * @param paper PAPER attribute (A_BG* value, 0x10-0x17).
 * @return (none) -- writes *w.
 */
void cwin_init(OricCharWin *w,
               uint8_t sx, uint8_t sy,
               uint8_t wx, uint8_t wy,
               uint8_t ink, uint8_t paper)
{
    if (sx < 2) sx = 2;
    w->sx    = sx;
    w->sy    = sy;
    w->wx    = wx;
    w->wy    = wy;
    w->cx    = 0;
    w->cy    = 0;
    w->ink   = ink;
    w->paper = paper;
}

// -------------------------------------------------------------------------
// Internal helpers
// -------------------------------------------------------------------------

/**
 * Write the INK and PAPER attribute bytes at columns 0 and 1 of an absolute
 * screen row.
 *
 * @param row   Absolute screen row (0-based, not window-relative).
 * @param ink   INK attribute (A_FW* value) written to column 0.
 * @param paper PAPER attribute (A_BG* value) written to column 1.
 * @return (none)
 */
static void row_setattr(uint8_t row, uint8_t ink, uint8_t paper)
{
    uint8_t *base = (uint8_t *)row_base[row];
    base[0] = ink;    // 0x00-0x07: INK attribute
    base[1] = paper;  // 0x10-0x17: PAPER attribute
}

// -------------------------------------------------------------------------
// Screen I/O
// -------------------------------------------------------------------------

/**
 * Clear a window: write the INK/PAPER attribute bytes at columns 0-1 of
 * every row, fill the content columns with spaces, and reset the cursor to
 * (0,0).
 *
 * @param w Window to clear.
 * @return (none)
 */
void cwin_clear(OricCharWin *w)
{
    for (uint8_t y = 0; y < w->wy; y++)
    {
        uint8_t  row  = w->sy + y;
        uint8_t *base = (uint8_t *)row_base[row];
        row_setattr(row, w->ink, w->paper);
        for (uint8_t x = w->sx; x < w->sx + w->wx; x++)
            base[x] = 0x20;   // space character
    }
    w->cx = 0;
    w->cy = 0;
}

/**
 * Like cwin_clear(), but also blanks the gap columns 2..(w->sx-1) when
 * w->sx > 2, instead of leaving them as whatever content was on screen
 * before the window opened. OricScreenEditorLOCI addition, not present
 * in locifilemanager-v2's original charwin.c: cwin_clear()'s narrow
 * clear (content only from w->sx onward) is the *correct* behaviour for
 * a deliberately narrow sidebar popup that wants the rest of the canvas
 * to stay visible underneath (src/charsetedit.c's CE_WIN_SX=27 is the
 * only window in this codebase that wants that) -- but every other
 * popup with sx > 2 (the various sx=5 dialogs: resize, goto, find/
 * replace, file save/load filename prompts, write-mode hex-attribute
 * entry) wants a fully opaque popup with no background bleeding through
 * on either side. This function fills from col 2 all the way to
 * SCREEN_COLS-1 (col 39) regardless of w->sx+w->wx, so narrower dialogs
 * (wx=30, right edge at col 34) also get cols 35-39 blanked.
 * Use this instead of cwin_clear() for any new sx > 2 popup that should
 * be fully opaque; keep using cwin_clear() for anything that should stay
 * a narrow, see-through sidebar.
 *
 * @param w Window to clear.
 * @return (none)
 */
void cwin_clear_full(OricCharWin *w)
{
    for (uint8_t y = 0; y < w->wy; y++)
    {
        uint8_t  row  = w->sy + y;
        uint8_t *base = (uint8_t *)row_base[row];
        row_setattr(row, w->ink, w->paper);
        base[2] = A_STD;   // reset charset-mode latch (persists across rows, does not
                            // reset per scanline -- col 2 is gap before sx>2 content)
        for (uint8_t x = 3; x < SCREEN_COLS; x++)
            base[x] = 0x20;   // space character
    }
    w->cx = 0;
    w->cy = 0;
}

/**
 * Write a single character at window-relative (x, y) without affecting the
 * cursor.
 *
 * @param w  Target window.
 * @param x  Window-relative column.
 * @param y  Window-relative row.
 * @param ch Character/attribute byte to write.
 * @return (none)
 */
void cwin_putat_char(OricCharWin *w, uint8_t x, uint8_t y, uint8_t ch)
{
    uint8_t *base = (uint8_t *)row_base[w->sy + y];
    base[w->sx + x] = ch;
}

/**
 * Read the character/attribute byte at window-relative (x, y).
 *
 * @param w Target window.
 * @param x Window-relative column.
 * @param y Window-relative row.
 * @return The byte currently stored at (x, y).
 */
uint8_t cwin_getat_char(OricCharWin *w, uint8_t x, uint8_t y)
{
    uint8_t *base = (uint8_t *)row_base[w->sy + y];
    return base[w->sx + x];
}

/**
 * Write a NUL-terminated string starting at window-relative (x, y), clipping
 * at the window's right edge. Cursor is not affected.
 *
 * @param w Target window.
 * @param x Window-relative starting column.
 * @param y Window-relative row.
 * @param s NUL-terminated string to write.
 * @return (none)
 */
void cwin_putat_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s)
{
    uint8_t *base = (uint8_t *)row_base[w->sy + y] + w->sx + x;
    uint8_t  rem  = w->wx - x;
    while (*s && rem--)
        *base++ = (uint8_t)*s++;
}

/**
 * Write a string in double-height characters at window-relative (x, y):
 * sets A_STD2H at column x on rows y and y+1, writes each character of s to
 * both rows (producing the top and bottom halves of each double-height
 * glyph), then restores A_STD on both rows at the column following the
 * string (if it fits). Does nothing if y+1 >= w->wy.
 *
 * @param w Target window.
 * @param x Window-relative starting column.
 * @param y Window-relative row of the top half (y+1 is the bottom half).
 * @param s NUL-terminated string to render in double-height.
 * @return (none)
 */
void cwin_putat_dblhi_string(OricCharWin *w, uint8_t x, uint8_t y, const char *s)
{
    if ((uint8_t)(y + 1) >= w->wy) return;
    uint8_t col = x;
    cwin_putat_char(w, col, y,       A_STD2H);
    cwin_putat_char(w, col, y + 1,   A_STD2H);
    col++;
    while (*s && col < w->wx)
    {
        cwin_putat_char(w, col, y,     (uint8_t)*s);
        cwin_putat_char(w, col, y + 1, (uint8_t)*s);
        col++;
        s++;
    }
    if (col < w->wx)
    {
        cwin_putat_char(w, col, y,     A_STD);
        cwin_putat_char(w, col, y + 1, A_STD);
    }
}

/**
 * Write a character at the cursor position and advance the cursor by one
 * column. Does nothing if the cursor is already at or past the right edge
 * (no wrap/scroll).
 *
 * @param w  Target window.
 * @param ch Character to write.
 * @return (none) -- writes one cell and advances w->cx.
 */
void cwin_put_char(OricCharWin *w, uint8_t ch)
{
    if (w->cx >= w->wx) return;
    cwin_putat_char(w, w->cx, w->cy, ch);
    w->cx++;
}

/**
 * Write a NUL-terminated string at the cursor via repeated cwin_put_char()
 * calls. No wrap/scroll.
 *
 * @param w Target window.
 * @param s NUL-terminated string to write.
 * @return (none)
 */
void cwin_put_string(OricCharWin *w, const char *s)
{
    while (*s)
        cwin_put_char(w, (uint8_t)*s++);
}

/**
 * Write a raw serial-attribute byte at the cursor and advance, via
 * cwin_put_char(). Use this instead of embedding the byte in a C string
 * literal when the attribute value is 0x00 (A_FWBLACK), which would
 * terminate the string early.
 *
 * @param w    Target window.
 * @param attr Attribute byte to write (e.g. an A_FW* or A_BG* constant).
 * @return (none)
 */
void cwin_put_attr(OricCharWin *w, uint8_t attr)
{
    cwin_put_char(w, attr);
}

// -------------------------------------------------------------------------
// Printf-style formatted output
// -------------------------------------------------------------------------

/**
 * Format fmt with the variadic arguments at fps into buf, supporting
 * %d/%u/%x/%s/%c/%% with an optional zero-fill/width prefix (e.g. "%02u").
 * No floating-point support (matches the -dNOFLOAT build). Always
 * NUL-terminates buf, truncating output if it would exceed maxlen-1
 * characters.
 *
 * @param buf    Destination buffer for the formatted string.
 * @param maxlen Size of buf in bytes, including the NUL terminator.
 * @param fmt    printf-style format string.
 * @param fps    Pointer to the first variadic argument (see comment below).
 * @return (none) -- result is written to buf.
 */
// fps: pointer to first variadic argument (Oscar64 native vararg convention —
//      caller passes (int *)&last_named_param + 1; no va_list/va_arg used
//      because Oscar64 native mode [-1] indexing in va_arg is unsupported).
static void _cwin_vformat(char *buf, uint8_t maxlen, const char *fmt, int *fps)
{
    char *p   = buf;
    char *end = buf + maxlen - 1;

    while (*fmt && p < end)
    {
        if (*fmt != '%') { *p++ = *fmt++; continue; }
        fmt++;   // skip '%'

        char fill  = ' ';
        uint8_t width = 0;
        if (*fmt == '0') { fill = '0'; fmt++; }
        while (*fmt >= '1' && *fmt <= '9')
            width = (uint8_t)(width * 10 + (*fmt++ - '0'));

        char spec = *fmt++;

        if (spec == 's')
        {
            const char *s = (const char *)*fps++;
            while (*s && p < end) *p++ = *s++;
        }
        else if (spec == 'c')
        {
            *p++ = (char)*fps++;
        }
        else if (spec == '%')
        {
            *p++ = '%';
        }
        else if (spec == 'd' || spec == 'u' || spec == 'x')
        {
            uint16_t uv = (uint16_t)*fps++;
            if (spec == 'd' && (int16_t)uv < 0)
            {
                if (p < end) *p++ = '-';
                uv = (uint16_t)(-(int16_t)uv);
            }
            uint8_t base   = (spec == 'x') ? 16 : 10;
            char    tmp[6];
            uint8_t digits = 0;
            if (uv == 0) tmp[digits++] = '0';
            while (uv) { uint8_t d = (uint8_t)(uv % base); tmp[digits++] = (char)(d < 10 ? '0'+d : 'A'+d-10); uv /= base; }
            uint8_t pad = (width > digits) ? (uint8_t)(width - digits) : 0;
            while (pad-- && p < end) *p++ = fill;
            while (digits && p < end) *p++ = tmp[--digits];
        }
    }
    *p = '\0';
}

/**
 * Format fmt with variadic arguments and write the result to the window via
 * cwin_console_put_string() (handles '\n', wraps, scrolls). Max 79 formatted
 * characters; see _cwin_vformat() for supported specifiers.
 *
 * @param w   Target window.
 * @param fmt printf-style format string (%d/%u/%x/%s/%c/%%, optional width).
 * @param ... Arguments matching fmt's specifiers.
 * @return (none)
 */
void cwin_printf(OricCharWin *w, const char *fmt, ...)
{
    static char pbuf[80];
    _cwin_vformat(pbuf, 80, fmt, (int *)&fmt + 1);
    cwin_console_put_string(w, pbuf);
}

/**
 * Format fmt with variadic arguments and write the result starting at
 * window-relative (x, y) via cwin_putat_string() (clips at the window's
 * right edge, no wrap/scroll). See _cwin_vformat() for supported specifiers.
 *
 * @param w   Target window.
 * @param x   Window-relative starting column.
 * @param y   Window-relative row.
 * @param fmt printf-style format string (%d/%u/%x/%s/%c/%%, optional width).
 * @param ... Arguments matching fmt's specifiers.
 * @return (none)
 */
void cwin_putat_printf(OricCharWin *w, uint8_t x, uint8_t y, const char *fmt, ...)
{
    static char pbuf[80];
    _cwin_vformat(pbuf, 80, fmt, (int *)&fmt + 1);
    cwin_putat_string(w, x, y, pbuf);
}

/**
 * Fill a bw x bh rectangle of character cells at window-relative (x, y) with
 * ch, clipped to the window's bounds. Attribute columns (0-1) are untouched
 * unless the rectangle itself covers them.
 *
 * @param w  Target window.
 * @param x  Window-relative starting column.
 * @param y  Window-relative starting row.
 * @param bw Rectangle width in characters.
 * @param bh Rectangle height in characters.
 * @param ch Character to fill with.
 * @return (none)
 */
void cwin_fill_rect(OricCharWin *w,
                    uint8_t x, uint8_t y,
                    uint8_t bw, uint8_t bh,
                    uint8_t ch)
{
    for (uint8_t row = 0; row < bh && (y + row) < w->wy; row++)
    {
        uint8_t *base = (uint8_t *)row_base[w->sy + y + row] + w->sx + x;
        for (uint8_t col = 0; col < bw && (x + col) < w->wx; col++)
            base[col] = ch;
    }
}

/**
 * Scroll the window's content up by one row: each row's content columns are
 * overwritten with the row below, and the new bottom row is cleared (attrs
 * refreshed via row_setattr(), content filled with spaces).
 *
 * @param w Target window.
 * @return (none)
 */
void cwin_scroll_up(OricCharWin *w)
{
    // Copy rows upward within window content columns only
    for (uint8_t y = 0; y < w->wy - 1; y++)
    {
        uint8_t *dst = (uint8_t *)row_base[w->sy + y]     + w->sx;
        uint8_t *src = (uint8_t *)row_base[w->sy + y + 1] + w->sx;
        for (uint8_t x = 0; x < w->wx; x++)
            dst[x] = src[x];
    }
    // Clear bottom row: attrs + spaces
    uint8_t last = w->sy + w->wy - 1;
    row_setattr(last, w->ink, w->paper);
    uint8_t *base = (uint8_t *)row_base[last] + w->sx;
    for (uint8_t x = 0; x < w->wx; x++)
        base[x] = 0x20;
}

/**
 * Scroll the window's content down by one row: each row's content columns
 * are overwritten with the row above (iterating bottom-to-top to avoid
 * clobbering source rows), and the new top row is cleared (attrs refreshed
 * via row_setattr(), content filled with spaces).
 *
 * @param w Target window.
 * @return (none)
 */
void cwin_scroll_down(OricCharWin *w)
{
    // Copy rows downward; iterate from bottom to top to avoid overwrite
    for (uint8_t y = w->wy - 1; y > 0; y--)
    {
        uint8_t *dst = (uint8_t *)row_base[w->sy + y]     + w->sx;
        uint8_t *src = (uint8_t *)row_base[w->sy + y - 1] + w->sx;
        for (uint8_t x = 0; x < w->wx; x++)
            dst[x] = src[x];
    }
    // Clear top row: attrs + spaces
    row_setattr(w->sy, w->ink, w->paper);
    uint8_t *base = (uint8_t *)row_base[w->sy] + w->sx;
    for (uint8_t x = 0; x < w->wx; x++)
        base[x] = 0x20;
}

/**
 * Insert a space at the cursor column on the cursor's row, shifting the rest
 * of the row's content right by one. The character at the right edge
 * (wx-1) is discarded. The cursor position is not moved.
 *
 * @param w Target window.
 * @return (none)
 */
void cwin_insert_char(OricCharWin *w)
{
    uint8_t *row = (uint8_t *)row_base[w->sy + w->cy] + w->sx;
    // Shift right from wx-2 down to cx; overflow at wx-1 is discarded
    for (uint8_t x = w->wx - 1; x > w->cx; x--)
        row[x] = row[x - 1];
    row[w->cx] = 0x20;
}

/**
 * Delete the character at the cursor column on the cursor's row, shifting
 * the rest of the row's content left by one. The right edge (wx-1) is filled
 * with a space. The cursor position is not moved.
 *
 * @param w Target window.
 * @return (none)
 */
void cwin_delete_char(OricCharWin *w)
{
    uint8_t *row = (uint8_t *)row_base[w->sy + w->cy] + w->sx;
    // Shift left from cx+1 to wx-1
    for (uint8_t x = w->cx; x < w->wx - 1; x++)
        row[x] = row[x + 1];
    row[w->wx - 1] = 0x20;
}

/**
 * Write a string at the cursor via cwin_put_string(), then advance to the
 * start of the next line via cwin_console_put_char('\n') (scrolling if
 * already on the last row).
 *
 * @param w Target window.
 * @param s NUL-terminated string to write.
 * @return (none)
 */
void cwin_printline(OricCharWin *w, const char *s)
{
    cwin_put_string(w, s);
    cwin_console_put_char(w, '\n');
}

// -------------------------------------------------------------------------
// Viewport
// -------------------------------------------------------------------------

/**
 * Initialize a viewport over a flat source character map, with the scroll
 * offset reset to (0,0). Does not draw; call cwin_viewport_blit() to render.
 *
 * @param vp           Viewport descriptor to initialize.
 * @param sourcebase   Pointer to the flat source character map
 *                       (sourcebase[row * sourcewidth + col]).
 * @param sourcewidth  Bytes per row in the source map (>= win->wx).
 * @param sourceheight Total rows in the source map.
 * @param win          Target display window the viewport blits into.
 * @return (none) -- writes *vp.
 */
void cwin_viewport_init(OricViewport *vp,
                        uint8_t *sourcebase,
                        uint16_t sourcewidth, uint16_t sourceheight,
                        OricCharWin *win)
{
    vp->sourcebase   = sourcebase;
    vp->sourcewidth  = sourcewidth;
    vp->sourceheight = sourceheight;
    vp->viewx        = 0;
    vp->viewy        = 0;
    vp->win          = win;
}

/**
 * Render the viewport's current win->wx x win->wy slice (at viewx, viewy) of
 * the source character map into the display window, refreshing each row's
 * INK/PAPER attributes. Rows past sourceheight, and columns past
 * sourcewidth, are filled with spaces; embedded NUL bytes in the source map
 * are also mapped to spaces.
 *
 * @param vp Viewport to blit.
 * @return (none)
 */
void cwin_viewport_blit(OricViewport *vp)
{
    OricCharWin *w = vp->win;
    for (uint8_t y = 0; y < w->wy; y++)
    {
        uint16_t srcrow = (uint16_t)(vp->viewy + y);
        if (srcrow >= vp->sourceheight)
        {
            // Past source data: write blank row
            row_setattr(w->sy + y, w->ink, w->paper);
            uint8_t *dst = (uint8_t *)row_base[w->sy + y] + w->sx;
            for (uint8_t x = 0; x < w->wx; x++)
                dst[x] = 0x20;
            continue;
        }
        uint8_t *src = vp->sourcebase + srcrow * vp->sourcewidth + vp->viewx;
        uint8_t *dst = (uint8_t *)row_base[w->sy + y] + w->sx;
        row_setattr(w->sy + y, w->ink, w->paper);
        // Visible columns (may be clipped if viewx+wx > sourcewidth)
        uint16_t avail = vp->sourcewidth - vp->viewx;
        uint8_t  cols  = (avail < w->wx) ? (uint8_t)avail : w->wx;
        for (uint8_t x = 0; x < cols; x++)
            dst[x] = src[x] ? src[x] : 0x20;   // map NUL → space
        for (uint8_t x = cols; x < w->wx; x++)
            dst[x] = 0x20;
    }
}

/**
 * Scroll the viewport by one unit in the given direction, clamped so the
 * view stays within the source map's bounds, then redraw via
 * cwin_viewport_blit(). Directions other than KEY_UP/DOWN/LEFT/RIGHT (or a
 * direction already at its bound) leave the offset unchanged but still
 * redraw.
 *
 * @param vp  Viewport to scroll.
 * @param dir One of KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT.
 * @return (none)
 */
void cwin_viewport_scroll(OricViewport *vp, uint8_t dir)
{
    OricCharWin *w    = vp->win;
    uint16_t     wx16 = w->wx;
    uint16_t     wy16 = w->wy;

    if      (dir == KEY_UP    && vp->viewy > 0)
        vp->viewy--;
    else if (dir == KEY_DOWN  && (uint16_t)(vp->viewy + wy16) < vp->sourceheight)
        vp->viewy++;
    else if (dir == KEY_LEFT  && vp->viewx > 0)
        vp->viewx--;
    else if (dir == KEY_RIGHT && (uint16_t)(vp->viewx + wx16) < vp->sourcewidth)
        vp->viewx++;
    cwin_viewport_blit(vp);
}

/**
 * Write ch as console output: '\n' moves to the start of the next line
 * (scrolling up if already on the last row); other characters are written at
 * the cursor and advance it, wrapping to the next line (scrolling if needed)
 * when the right edge is reached. Refreshes the new row's INK/PAPER
 * attributes whenever the cursor moves to a new row.
 *
 * @param w  Target window.
 * @param ch Character to write, or '\n' for a line break.
 * @return (none)
 */
void cwin_console_put_char(OricCharWin *w, uint8_t ch)
{
    if (ch == '\n')
    {
        w->cx = 0;
        if (w->cy < w->wy - 1)
        {
            w->cy++;
            row_setattr(w->sy + w->cy, w->ink, w->paper);
        }
        else
        {
            cwin_scroll_up(w);
        }
        return;
    }

    cwin_putat_char(w, w->cx, w->cy, ch);
    w->cx++;
    if (w->cx >= w->wx)
    {
        w->cx = 0;
        if (w->cy < w->wy - 1)
        {
            w->cy++;
            row_setattr(w->sy + w->cy, w->ink, w->paper);
        }
        else
        {
            cwin_scroll_up(w);
        }
    }
}

/**
 * Write a NUL-terminated string via repeated cwin_console_put_char() calls
 * (handles '\n', wraps, scrolls).
 *
 * @param w Target window.
 * @param s NUL-terminated string to write.
 * @return (none)
 */
void cwin_console_put_string(OricCharWin *w, const char *s)
{
    while (*s)
        cwin_console_put_char(w, (uint8_t)*s++);
}

// -------------------------------------------------------------------------
// Cursor
// -------------------------------------------------------------------------

/**
 * Toggle the visual cursor at the current cursor cell by inverting the
 * character byte's bit 7 (mapping space <-> inverse-space block). The caller
 * must track on/off state itself to avoid double-toggling the same cell.
 *
 * @param w  Target window.
 * @param on true to show the cursor (set inverse video), false to hide it
 *            (restore normal video).
 * @return (none)
 */
void cwin_cursor_show(OricCharWin *w, bool on)
{
    uint8_t *cell = (uint8_t *)row_base[w->sy + w->cy] + w->sx + w->cx;
    if (on)
    {
        // Invert: space→inverse-space block, other chars set bit 7
        *cell = (*cell == 0x20) ? 0xA0 : (*cell | 0x80);
    }
    else
    {
        // Restore: clear bit 7, map inverse-space back to space
        uint8_t c = *cell & 0x7F;
        *cell = (c == 0x00) ? 0x20 : c;   // 0xA0 & 0x7F = 0x00 → space
    }
}

/**
 * Move the cursor one column left within the current row.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already at column 0.
 */
bool cwin_cursor_left(OricCharWin *w)
{
    if (w->cx == 0) return false;
    w->cx--;
    return true;
}

/**
 * Move the cursor one column right within the current row.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already at the rightmost
 *         column (wx-1).
 */
bool cwin_cursor_right(OricCharWin *w)
{
    if (w->cx >= w->wx - 1) return false;
    w->cx++;
    return true;
}

/**
 * Move the cursor one row up.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already on row 0.
 */
bool cwin_cursor_up(OricCharWin *w)
{
    if (w->cy == 0) return false;
    w->cy--;
    return true;
}

/**
 * Move the cursor one row down.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already on the bottom
 *         row (wy-1).
 */
bool cwin_cursor_down(OricCharWin *w)
{
    if (w->cy >= w->wy - 1) return false;
    w->cy++;
    return true;
}

// -------------------------------------------------------------------------
// Cursor — extended movement
// -------------------------------------------------------------------------

/**
 * Move the cursor directly to window-relative (cx, cy), without bounds
 * checking or redrawing.
 *
 * @param w  Target window.
 * @param cx New cursor column.
 * @param cy New cursor row.
 * @return (none)
 */
void cwin_cursor_move(OricCharWin *w, uint8_t cx, uint8_t cy)
{
    w->cx = cx;
    w->cy = cy;
}

/**
 * Advance the cursor forward by one cell, wrapping to the start of the next
 * row at the right edge. Does not scroll.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already at the
 *         bottom-right cell (wx-1, wy-1).
 */
bool cwin_cursor_forward(OricCharWin *w)
{
    if (w->cx + 1 < w->wx) { w->cx++; return true; }
    if (w->cy + 1 < w->wy) { w->cx = 0; w->cy++; return true; }
    return false;
}

/**
 * Retreat the cursor backward by one cell, wrapping to the end of the
 * previous row at the left edge.
 *
 * @param w Target window.
 * @return true if the cursor moved, false if it was already at the top-left
 *         cell (0, 0).
 */
bool cwin_cursor_backward(OricCharWin *w)
{
    if (w->cx > 0) { w->cx--; return true; }
    if (w->cy > 0) { w->cx = w->wx - 1; w->cy--; return true; }
    return false;
}

/**
 * Move the cursor to column 0 of the next row, without scrolling.
 *
 * @param w Target window.
 * @return true if the cursor moved to a new row, false if it was already on
 *         the last row (only cx is reset to 0 in that case).
 */
bool cwin_cursor_newline(OricCharWin *w)
{
    w->cx = 0;
    if (w->cy + 1 < w->wy) { w->cy++; return true; }
    return false;
}

// -------------------------------------------------------------------------
// Multi-character bulk I/O
// -------------------------------------------------------------------------

/**
 * Write exactly num characters at the cursor via repeated cwin_put_char()
 * calls, advancing the cursor. No wrap/scroll.
 *
 * @param w     Target window.
 * @param chars Buffer of characters to write (need not be NUL-terminated).
 * @param num   Number of characters to write.
 * @return (none)
 */
void cwin_put_chars(OricCharWin *w, const char *chars, uint8_t num)
{
    for (uint8_t i = 0; i < num; i++)
        cwin_put_char(w, (uint8_t)chars[i]);
}

/**
 * Write exactly num characters starting at window-relative (x, y), clipped
 * to the window's right edge. Cursor is not affected.
 *
 * @param w     Target window.
 * @param x     Window-relative starting column.
 * @param y     Window-relative row.
 * @param chars Buffer of characters to write (need not be NUL-terminated).
 * @param num   Number of characters to write (clipped at wx - x).
 * @return (none)
 */
void cwin_putat_chars(OricCharWin *w, uint8_t x, uint8_t y,
                      const char *chars, uint8_t num)
{
    uint8_t *base = (uint8_t *)row_base[w->sy + y] + w->sx + x;
    uint8_t rem = (uint8_t)(w->wx - x);
    uint8_t n = (num < rem) ? num : rem;
    for (uint8_t i = 0; i < n; i++)
        base[i] = (uint8_t)chars[i];
}

/**
 * Read exactly num characters starting at window-relative (x, y) into chars,
 * clipped to the window's right edge. The output buffer is not
 * NUL-terminated.
 *
 * @param w     Target window.
 * @param x     Window-relative starting column.
 * @param y     Window-relative row.
 * @param chars Destination buffer (at least num bytes).
 * @param num   Number of characters to read (clipped at wx - x).
 * @return (none) -- result is written to chars.
 */
void cwin_getat_chars(OricCharWin *w, uint8_t x, uint8_t y,
                      char *chars, uint8_t num)
{
    uint8_t *base = (uint8_t *)row_base[w->sy + y] + w->sx + x;
    uint8_t rem = (uint8_t)(w->wx - x);
    uint8_t n = (num < rem) ? num : rem;
    for (uint8_t i = 0; i < n; i++)
        chars[i] = (char)base[i];
}

// -------------------------------------------------------------------------
// Rectangle copy (character bytes only — no separate colour RAM on Oric)
// -------------------------------------------------------------------------

/**
 * Copy a bw x bh rectangle of character bytes from window-relative (x, y)
 * into a flat row-major buffer (bw bytes per row, bh rows), clipped to the
 * window's bounds. Attribute bytes are not read/copied.
 *
 * @param w     Source window.
 * @param x     Window-relative starting column.
 * @param y     Window-relative starting row.
 * @param bw    Rectangle width in characters (row stride of chars).
 * @param bh    Rectangle height in characters.
 * @param chars Destination buffer, at least bw * bh bytes.
 * @return (none) -- result is written to chars.
 */
void cwin_get_rect(OricCharWin *w, uint8_t x, uint8_t y,
                   uint8_t bw, uint8_t bh, char *chars)
{
    for (uint8_t row = 0; row < bh && (y + row) < w->wy; row++)
    {
        uint8_t *base = (uint8_t *)row_base[w->sy + y + row] + w->sx + x;
        uint8_t rem = (uint8_t)(w->wx - x);
        uint8_t n = (bw < rem) ? bw : rem;
        for (uint8_t col = 0; col < n; col++)
            chars[col] = (char)base[col];
        chars += bw;
    }
}

/**
 * Write a flat row-major buffer of characters (bw bytes per row, bh rows)
 * into the window at (x, y), clipped to the window's bounds. Attribute bytes
 * are not touched.
 *
 * @param w     Target window.
 * @param x     Window-relative starting column.
 * @param y     Window-relative starting row.
 * @param bw    Rectangle width in characters (row stride of chars).
 * @param bh    Rectangle height in characters.
 * @param chars Source buffer, bw * bh bytes.
 * @return (none)
 */
void cwin_put_rect(OricCharWin *w, uint8_t x, uint8_t y,
                   uint8_t bw, uint8_t bh, const char *chars)
{
    for (uint8_t row = 0; row < bh && (y + row) < w->wy; row++)
    {
        uint8_t *base = (uint8_t *)row_base[w->sy + y + row] + w->sx + x;
        uint8_t rem = (uint8_t)(w->wx - x);
        uint8_t n = (bw < rem) ? bw : rem;
        for (uint8_t col = 0; col < n; col++)
            base[col] = (uint8_t)chars[col];
        chars += bw;
    }
}

// -------------------------------------------------------------------------
// Word-wrap print
//
// Adapted from vdcwin_printwrap by Xander Mol (Oscar64Test/include/vdc_win.c),
// originally from C3L by Steven P. Goldsmith:
// https://github.com/sgjava/c3l/blob/main/src/conww.c
// Adapted: Oric charwin API; no strlen/strcpy (bare-metal, no string.h).
// -------------------------------------------------------------------------

/**
 * Print str into the window with word-wrapping at spaces, using console
 * output (cwin_console_put_char()/cwin_console_put_string(), so it wraps and
 * scrolls automatically). Words longer than the window width are
 * hard-split across lines. Does not add a trailing newline.
 *
 * @param w   Target window.
 * @param str NUL-terminated string to print.
 * @return (none)
 */
void cwin_printwrap(OricCharWin *w, const char *str)
{
    char    wrapbuffer[42];   // max wx=38 + space + NUL with margin
    uint8_t i = 0, buf = 0;
    uint8_t len = 0;
    while (str[len]) len++;
    int8_t  wordStart = -1, wordEnd = -1;
    uint8_t maxLine = w->wx;
    uint8_t maxBuf  = (uint8_t)(sizeof(wrapbuffer) - 1);

    while (i < len && buf < maxBuf)
    {
        while (i < len && wordEnd < 0 && buf < maxBuf)
        {
            if (str[i] != ' ')
            {
                if (wordStart < 0) wordStart = (int8_t)i;
                wrapbuffer[buf++] = str[i];
            }
            else
            {
                if (wordEnd < 0)
                {
                    wrapbuffer[buf++] = str[i];
                    wordEnd = (int8_t)i;
                }
            }
            i++;
        }

        if (buf > 0)
        {
            wrapbuffer[buf] = '\0';
            uint8_t wordLen = 0;
            while (wrapbuffer[wordLen]) wordLen++;

            // Split words that exceed the window width
            while (wordLen > w->wx)
            {
                cwin_console_put_char(w, '\n');
                cwin_put_chars(w, wrapbuffer, w->wx);
                // Shift remaining content to start of buffer
                uint8_t k, shift = w->wx;
                for (k = 0; wrapbuffer[shift + k]; k++)
                    wrapbuffer[k] = wrapbuffer[shift + k];
                wrapbuffer[k] = '\0';
                wordLen = k;
            }

            if ((uint8_t)(w->cx + wordLen) > maxLine)
                cwin_console_put_char(w, '\n');
            cwin_console_put_string(w, wrapbuffer);
            wordStart = -1;
            wordEnd   = -1;
            buf       = 0;
        }
    }
}

// -------------------------------------------------------------------------
// Horizontal scroll
// -------------------------------------------------------------------------

/**
 * Shift every row's content columns left by `by` columns, filling the
 * vacated columns at the right edge with spaces. Attribute columns (0-1) are
 * not touched. A `by` >= wx clears the entire window content.
 *
 * @param w  Target window.
 * @param by Number of columns to shift left (clamped to wx).
 * @return (none)
 */
void cwin_scroll_left(OricCharWin *w, uint8_t by)
{
    if (by == 0) return;
    if (by >= w->wx) by = w->wx;
    uint8_t keep = (uint8_t)(w->wx - by);
    for (uint8_t y = 0; y < w->wy; y++)
    {
        uint8_t *row = (uint8_t *)row_base[w->sy + y] + w->sx;
        for (uint8_t x = 0; x < keep; x++)
            row[x] = row[x + by];
        for (uint8_t x = keep; x < w->wx; x++)
            row[x] = 0x20;
    }
}

/**
 * Shift every row's content columns right by `by` columns (iterating
 * right-to-left to avoid overwriting source data), filling the vacated
 * columns at the left edge with spaces. Attribute columns (0-1) are not
 * touched. A `by` >= wx clears the entire window content. A `by` of 0 is a
 * no-op.
 *
 * @param w  Target window.
 * @param by Number of columns to shift right (clamped to wx).
 * @return (none)
 */
void cwin_scroll_right(OricCharWin *w, uint8_t by)
{
    if (by == 0) return;
    if (by >= w->wx) by = w->wx;
    for (uint8_t y = 0; y < w->wy; y++)
    {
        uint8_t *row = (uint8_t *)row_base[w->sy + y] + w->sx;
        // Iterate right-to-left to avoid overwriting source data
        for (uint8_t x = w->wx - 1; x >= by; x--)
            row[x] = row[x - by];
        for (uint8_t x = 0; x < by; x++)
            row[x] = 0x20;
    }
}

// -------------------------------------------------------------------------
// Overlay RAM save/restore — REQUIRES LOCI device (not available in Oricutron)
// SEI/CLI brackets the overlay-RAM window; ROM is invisible during that time.
// -------------------------------------------------------------------------

/**
 * Save window w's full screen rows [sy, sy+wy) to LOCI overlay RAM (LIFO),
 * for later restoration via cwin_pop(). Requires a LOCI device (overlay RAM
 * is mapped in via MICRODISCCFG). Disables interrupts (PHP/SEI/PLP, not
 * SEI/CLI -- see the comment below and menu.c menu_winsave()) for the
 * duration of the overlay-RAM access. Silently does nothing if the save
 * stack (depth OVERLAY_STACK_DEPTH) is full.
 *
 * @param w Window whose rows [sy, sy+wy) are saved.
 * @return (none) -- copies SCREEN_COLS * wy bytes to overlay RAM and pushes
 *         a SaveRecord.
 */
void cwin_push(OricCharWin *w)
{
    if (save_depth >= OVERLAY_STACK_DEPTH) return;

    // Compute size as addition to avoid 16-bit multiply on 6502
    uint16_t size = 0;
    for (uint8_t i = 0; i < w->wy; i++) size = (uint16_t)(size + SCREEN_COLS);
    uint16_t src_addr = row_base[w->sy];
    uint16_t dst_addr = overlay_sp;

    save_stack[save_depth].sy   = w->sy;
    save_stack[save_depth].wy   = w->wy;
    save_stack[save_depth].addr = dst_addr;
    save_stack[save_depth].size = size;
    save_depth++;

    // PHP/PLP, not SEI/CLI: see menu.c menu_winsave() for why an unconditional
    // CLI here would permanently re-enable IRQs (no IRQ handler is installed,
    // so the stock ROM IRQ handler would then run every frame and corrupt
    // zero page / screen RAM). PLP restores the prior interrupt-disable state.
    __asm { php }
    __asm { sei }
    MICRODISCCFG = OVERLAY_ON;

    uint8_t *src = (uint8_t *)src_addr;
    uint8_t *dst = (uint8_t *)dst_addr;
    for (uint16_t i = 0; i < size; i++)
        dst[i] = src[i];

    overlay_sp  += size;
    MICRODISCCFG = OVERLAY_OFF;
    __asm { plp }
}

/**
 * Restore the most recently cwin_push()-ed rows from LOCI overlay RAM back
 * to screen RAM (LIFO), reversing cwin_push(). Requires a LOCI device.
 * Disables interrupts (PHP/SEI/PLP -- see cwin_push()) for the duration of
 * the overlay-RAM access. Silently does nothing if the save stack is empty.
 *
 * @param w Unused; the saved geometry (start row, height) is taken from the
 *           save stack, not from w.
 * @return (none) -- copies the saved rows back from overlay RAM and pops a
 *         SaveRecord.
 */
void cwin_pop(OricCharWin *w)
{
    if (save_depth == 0) return;

    save_depth--;
    uint16_t src_addr = save_stack[save_depth].addr;
    uint16_t size     = save_stack[save_depth].size;
    uint8_t  sy       = save_stack[save_depth].sy;
    uint16_t dst_addr = row_base[sy];

    // PHP/PLP required — see cwin_push() above.
    __asm { php }
    __asm { sei }
    MICRODISCCFG = OVERLAY_ON;

    uint8_t *src = (uint8_t *)src_addr;
    uint8_t *dst = (uint8_t *)dst_addr;
    for (uint16_t i = 0; i < size; i++)
        dst[i] = src[i];

    overlay_sp  -= size;
    MICRODISCCFG = OVERLAY_OFF;
    __asm { plp }

    (void)w;  // w is unused; geometry is stored in save_stack
}

// -------------------------------------------------------------------------
// Key input
// -------------------------------------------------------------------------

/**
 * Block until a key is pressed and return it, via keyb_getch().
 *
 * @return The key code read.
 */
uint8_t cwin_getch(void)
{
    return keyb_getch();
}

// -------------------------------------------------------------------------
// Text input widget
//
// Based on v1 locifilemanager textInput() by Xander Mol, adapted from
// DraBrowse 1.0e by Sascha Bader (2009). Adapted for charwin API and Oscar64.
// -------------------------------------------------------------------------

/**
 * Interactive single-line text input widget at window-relative (x, y), with
 * a vwidth-wide scrolling viewport over a buffer of up to maxlen characters.
 * Displays an inverse-video cursor over the character at the edit position
 * (or an inverse space past the end of the string). Handles ENTER (accept),
 * ESC (cancel), DEL (backspace), LEFT/RIGHT (move cursor), and printable
 * character insertion/overwrite filtered by `validation`.
 *
 * @param w          Target window.
 * @param x          Window-relative column of the input field.
 * @param y          Window-relative row of the input field.
 * @param vwidth     Visible width of the input field in characters (may be
 *                     less than maxlen, enabling horizontal scrolling).
 * @param str        Pre-initialized, NUL-terminated buffer of at least
 *                     maxlen+1 bytes; edited in place. The cursor starts
 *                     after the existing content.
 * @param maxlen     Maximum string length, not counting the NUL terminator.
 * @param validation Bitwise combination of VINPUT_* flags restricting which
 *                     characters may be typed (0/VINPUT_ALL = all
 *                     printable).
 * @return The resulting string length on ENTER, or -1 if cancelled with ESC
 *         (str is left unchanged on ESC, but the field is blanked on
 *         screen).
 */
signed int cwin_textinput(OricCharWin *w,
                          uint8_t x, uint8_t y,
                          uint8_t vwidth,
                          char *str, uint8_t maxlen,
                          uint8_t validation)
{
    uint8_t idx = 0;
    while (str[idx]) idx++;   // position cursor at existing content end

    while (1)
    {
        // Count current length
        uint8_t len = 0;
        while (str[len]) len++;

        // Viewport offset: keep cursor visible in right portion
        uint8_t offs = (idx + 1 > vwidth) ? (uint8_t)(idx + 1 - vwidth) : 0;

        // Redraw viewport: clear, print vwidth-1 visible chars, draw cursor
        for (uint8_t i = 0; i < vwidth; i++)
            cwin_putat_char(w, x + i, y, 0x20);

        for (uint8_t i = 0; i < vwidth - 1 && str[offs + i]; i++)
            cwin_putat_char(w, x + i, y, (uint8_t)str[offs + i]);

        uint8_t cur_ch = str[idx] ? (uint8_t)(str[idx] | 0x80) : 0xA0;
        cwin_putat_char(w, x + (uint8_t)(idx - offs), y, cur_ch);

        // Get key
        uint8_t c = cwin_getch();

        switch (c)
        {
        case KEY_ESC:
            for (uint8_t i = 0; i < vwidth; i++)
                cwin_putat_char(w, x + i, y, 0x20);
            return -1;

        case KEY_ENTER:
            for (uint8_t i = 0; i < vwidth; i++)
                cwin_putat_char(w, x + i, y, 0x20);
            for (uint8_t i = 0; i < vwidth && str[i]; i++)
                cwin_putat_char(w, x + i, y, (uint8_t)str[i]);
            return (signed int)len;

        case KEY_DEL:
            if (idx > 0)
            {
                idx--;
                uint8_t i = idx;
                while (str[i + 1]) { str[i] = str[i + 1]; i++; }
                str[i] = 0;
            }
            break;

        case KEY_LEFT:
            if (idx > 0) idx--;
            break;

        case KEY_RIGHT:
            if (idx < len && idx < maxlen) idx++;
            break;

        default:
        {
            bool valid = (validation == VINPUT_ALL);
            if ((validation & VINPUT_NUMS)  && c >= '0' && c <= '9') valid = true;
            if ((validation & VINPUT_ALPHA) && c >= 'A' && c <= 'Z') valid = true;
            if ((validation & VINPUT_ALPHA) && c >= 'a' && c <= 'z') valid = true;
            if ((validation & VINPUT_WILD)  && (c == '*' || c == '?')) valid = true;

            if (valid && c >= 0x20 && c <= 0x7E && idx < maxlen)
            {
                // Overwrite at idx; extend string if at null terminator
                if (str[idx] == 0) str[idx + 1] = 0;
                str[idx] = (char)c;
                idx++;
            }
            break;
        }
        }
    }
    return 0;
}
