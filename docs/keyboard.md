# Keyboard scanner (keyboard.h)

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
