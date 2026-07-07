# IJK joystick (ijk.h)

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
found.  Call once after startup (`src/main.c` calls this before checking
`loci_present()`, see [loci.md](loci.md)).

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
