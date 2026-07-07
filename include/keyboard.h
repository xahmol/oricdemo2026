// keyboard.h - Oric Atmos keyboard scanner (direct VIA, no ROM)
//
// The scanner drives AY register $0E for column selection, reads VIA Port B
// bit 3 for key sense, and VIA Port B bits 0-2 for row selection.
//
// Based on LOCI ROM keyboard.s by Sodiumlightbaby, 2024
// https://github.com/sodiumlb/loci-rom
// Adapted: C + Oscar64 inline asm; polling (not IRQ-driven); no AY init.

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// -------------------------------------------------------------------------
// Key codes — compatible with v1 locifilemanager (CC65 atmos.h CH_* values)
// -------------------------------------------------------------------------

#define KEY_NONE        0x00
#define KEY_ENTER       0x0D    // Return key
#define KEY_ESC         0x1B    // Escape key
#define KEY_DEL         0x7F    // Delete/backspace key
#define KEY_UP          0x0B    // Cursor up    (CH_CURS_UP)
#define KEY_DOWN        0x0A    // Cursor down  (CH_CURS_DOWN)
#define KEY_LEFT        0x08    // Cursor left  (CH_CURS_LEFT)
#define KEY_RIGHT       0x09    // Cursor right (CH_CURS_RIGHT)
#define KEY_SPACE       0x20    // Space bar
#define KEY_TAB         0x09    // Tab (same as RIGHT — context-dependent)

// FUNCT + number key (F1–F10), compatible with v1 CH_F1..CH_F0
#define KEY_F1          0xB1    // FUNCT + 1
#define KEY_F2          0xB2
#define KEY_F3          0xB3
#define KEY_F4          0xB4
#define KEY_F5          0xB5
#define KEY_F6          0xB6
#define KEY_F7          0xB7
#define KEY_F8          0xB8
#define KEY_F9          0xB9
#define KEY_F10         0xB0    // FUNCT + 0

// Control codes: CTRL + letter = letter & 0x1F
#define KEY_CTRL_A      0x01
#define KEY_CTRL_B      0x02
#define KEY_CTRL_C      0x03
#define KEY_CTRL_D      0x04
#define KEY_CTRL_R      0x12
#define KEY_CTRL_V      0x16
#define KEY_CTRL_X      0x18
#define KEY_CTRL_Z      0x1A

// -------------------------------------------------------------------------
// Modifier bit flags (keyb_modifiers)
// -------------------------------------------------------------------------

#define MOD_SHIFT       0x01    // Left or right SHIFT held
#define MOD_CTRL        0x02    // Left or right CTRL held
#define MOD_FUNCT       0x04    // FUNCTION key held (Atmos only)
#define MOD_CAPSLOCK    0x08    // Caps Lock active (toggle, not held)

// -------------------------------------------------------------------------
// Key matrix positions for modifier detection
// position = row * 8 + col
// -------------------------------------------------------------------------

#define VKEY_LCTRL      (2*8+4)    // 20  — Left CTRL
#define VKEY_RCTRL      (0*8+4)    //  4  — Right CTRL
#define VKEY_LSHIFT     (4*8+4)    // 36  — Left SHIFT
#define VKEY_RSHIFT     (7*8+4)    // 60  — Right SHIFT
#define VKEY_FUNCT      (5*8+4)    // 44  — FUNCTION key (Atmos only)

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

// Raw keyboard matrix — 8 bytes, one per row.
// Bit N of keyb_matrix[R] = 1 if key at (row R, col N) is pressed.
// Populated by keyb_scan().
extern uint8_t keyb_matrix[8];

// Current decoded ASCII character from last keyb_poll() call.
// 0 if no key pressed or key is a pure modifier.
extern volatile uint8_t keyb_char;

// Modifier state after last keyb_poll() call.
extern uint8_t keyb_modifiers;

// Scan the full 8×8 key matrix into keyb_matrix[].
// Takes ~1–2 ms (64 AY+VIA write/read cycles at 1 MHz).
// Call from polling loop or IRQ.
void keyb_scan(void);

// Decode keyb_matrix[] into keyb_char and keyb_modifiers.
// Handles SHIFT, CTRL, FUNCT, Caps Lock.
// Returns decoded ASCII code (0 = no key).
uint8_t keyb_decode(void);

// Poll keyboard: scan + decode + handle key repeat.
// keyb_char is updated; returns same value.
// Repeat fires after ~400 ms initial delay, then every ~100 ms.
uint8_t keyb_poll(void);

// Blocking: loop calling keyb_poll() until a key is pressed.
// Returns the decoded key code.
uint8_t keyb_getch(void);

// Check if key is pressed without consuming it.
// Returns non-zero if a key is currently held.
uint8_t keyb_check(void);

#pragma compile("keyboard.c")

#endif  // KEYBOARD_H
