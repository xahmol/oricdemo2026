# LOCI mass-storage API (loci.h)

High-level C API for the LOCI mass-storage device.  Include `loci.h`.
`src/main.c` calls `loci_present()` at boot to detect a LOCI device and
report its status; the rest of this API (file/directory/mount/tape
operations) is available but not yet exercised by this project's demo code.

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
Always disable before returning to normal ROM execution. Not currently used
by this project's demo code, but available for future overlay-RAM use
(e.g. window save/restore, see [charwin.md](charwin.md)'s `cwin_push`/`cwin_pop`).

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
