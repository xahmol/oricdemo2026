// loci.h - LOCI mass-storage device API for Oscar64 / Oric Atmos (bare-metal)
//
// Based on:
//   LOCI ROM by Sodiumlightbaby, 2024  https://github.com/sodiumlb/loci-rom
//   Picocomputer 6502 by Rumbledethumps, 2023  https://github.com/picocomputer/rp6502
//   locifilemanager v1 by Xander Mol, 2025  (CC65 port of LOCI API)
//
// Adapted: C translation for Oscar64 native mode (-n); no CC65 calling conventions;
// helpers replace mia.s assembly; stdint types throughout.
// Note: struct fields 'a' and 'x' renamed to 'areg'/'xreg' — Oscar64 native mode
// treats 'a' and 'x' as 6502 register identifiers and cannot parse them as
// struct member names.

#ifndef LOCI_H
#define LOCI_H

#include <stdint.h>
#include <stdbool.h>
#include "oric.h"

// ─────────────────────────────────────────────────────────────────────────────
// MIA — Mass Interface Adapter at $03A0
// ─────────────────────────────────────────────────────────────────────────────

struct __LOCI_MIA
{
    const uint8_t ready;    // +00 $03A0: RX/TX ready bits (read-only)
    uint8_t       tx;       // +01 $03A1: transmit byte to MIA firmware
    const uint8_t rx;       // +02 $03A2: receive byte from firmware (read-only)
    const uint8_t vsync;    // +03 $03A3: vsync flag (read-only)
    uint8_t       rw0;      // +04 $03A4: DMA channel 0 read/write data
    uint8_t       step0;    // +05 $03A5: DMA channel 0 address step (1=forward)
    uint16_t      addr0;    // +06 $03A6: DMA channel 0 XRAM address (16-bit LE)
    uint8_t       rw1;      // +08 $03A8: DMA channel 1 read/write data
    uint8_t       step1;    // +09 $03A9: DMA channel 1 address step
    uint16_t      addr1;    // +0A $03AA: DMA channel 1 XRAM address
    uint8_t       xstack;   // +0C $03AC: XSTACK — parameter/result exchange byte
    uint8_t       errno_lo; // +0D $03AD: error code low byte
    uint8_t       errno_hi; // +0E $03AE: error code high byte
    uint8_t       op;       // +0F $03AF: operation code (write to invoke)
    uint8_t       irq;      // +10 $03B0: interrupt control
    const uint8_t spin;     // +11 $03B1: ROM spin-loop entry (JSR here to wait)
    const uint8_t busy;     // +12 $03B2: busy flag — bit 7 set while operation runs
    const uint8_t lda_op;   // +13 $03B3: 0xA9 (LDA #imm opcode, read-only)
    uint8_t       areg;     // +14 $03B4: MIA A register (argument / result low byte)
    const uint8_t ldx_op;   // +15 $03B5: 0xA2 (LDX #imm opcode, read-only)
    uint8_t       xreg;     // +16 $03B6: MIA X register (argument / result high byte)
    const uint8_t rts_op;   // +17 $03B7: 0x60 (RTS opcode, read-only)
    uint16_t      sreg;     // +18 $03B8: status register (upper 16 bits of 32-bit result)
};
#define MIA (*(volatile struct __LOCI_MIA *)0x03A0)

#define MIA_BUSY_BIT   0x80
#define MIA_READY_TX   0x80
#define MIA_READY_RX   0x40

// LOCI presence: ROM places 'L' at $0319 when device is active
#define LOCI_SIGNATURE_ADDR ((volatile const uint8_t *)0x0319)

// ─────────────────────────────────────────────────────────────────────────────
// TAP — Tape controller at $0315
// ─────────────────────────────────────────────────────────────────────────────

struct __LOCI_TAP
{
    uint8_t cmd;    // +0 $0315: command
    uint8_t status; // +1 $0316: status
    uint8_t data;   // +2 $0317: data
};
#define TAP (*(volatile struct __LOCI_TAP *)0x0315)

#define TAP_CMD_PLAY 0x01
#define TAP_CMD_REC  0x02
#define TAP_CMD_REW  0x03
#define TAP_CMD_BIT  0x04
#define TAP_CMD_FFW  0x05

// ─────────────────────────────────────────────────────────────────────────────
// MIA operation codes
// ─────────────────────────────────────────────────────────────────────────────

#define MIA_OP_ZXSTACK           0x00
#define MIA_OP_XREG              0x01
#define MIA_OP_PHI2              0x02
#define MIA_OP_CODEPAGE          0x03
#define MIA_OP_LRAND             0x04
#define MIA_OP_STDIN_OPT         0x05
#define MIA_OP_CLOCK_GETRES      0x10
#define MIA_OP_CLOCK_GETTIME     0x11
#define MIA_OP_CLOCK_SETTIME     0x12
#define MIA_OP_CLOCK_GETTIMEZONE 0x13
#define MIA_OP_OPEN              0x14
#define MIA_OP_CLOSE             0x15
#define MIA_OP_READ_XSTACK       0x16
#define MIA_OP_READ_XRAM         0x17
#define MIA_OP_WRITE_XSTACK      0x18
#define MIA_OP_WRITE_XRAM        0x19
#define MIA_OP_LSEEK             0x1A
#define MIA_OP_UNLINK            0x1B
#define MIA_OP_RENAME            0x1C
#define MIA_OP_OPENDIR           0x80
#define MIA_OP_CLOSEDIR          0x81
#define MIA_OP_READDIR           0x82
#define MIA_OP_MKDIR             0x83
#define MIA_OP_GETCWD            0x88
#define MIA_OP_MOUNT             0x90
#define MIA_OP_UMOUNT            0x91
#define MIA_OP_TAP_SEEK          0x92
#define MIA_OP_TAP_TELL          0x93
#define MIA_OP_TAP_HDR           0x94
#define MIA_OP_UNAME             0x98
#define MIA_OP_BOOT              0xA0
#define MIA_OP_TUNE_TMAP         0xA1
#define MIA_OP_TUNE_TIOR         0xA2
#define MIA_OP_TUNE_TIOW         0xA3
#define MIA_OP_TUNE_TIOD         0xA4
#define MIA_OP_TUNE_TADR         0xA5
#define MIA_OP_TUNE_SCAN         0xA6
#define MIA_OP_EXIT              0xFF

// ─────────────────────────────────────────────────────────────────────────────
// File open flags (O_* values match LOCI ROM expectations)
// ─────────────────────────────────────────────────────────────────────────────

#define O_RDONLY  0x01
#define O_WRONLY  0x02
#define O_RDWR    0x03
#define O_CREAT   0x10
#define O_TRUNC   0x20
#define O_APPEND  0x40
#define O_EXCL    0x80

// Seek whence values (match LOCI ROM)
#define SEEK_CUR  0
#define SEEK_END  1
#define SEEK_SET  2

// Directory entry attribute bits
#define DIR_ATTR_RDO  0x01
#define DIR_ATTR_SYS  0x04
#define DIR_ATTR_DIR  0x10

// ─────────────────────────────────────────────────────────────────────────────
// Data structures
// ─────────────────────────────────────────────────────────────────────────────

// Tape file header (25 bytes) — matches Oric tape format
typedef struct
{
    uint8_t flag_int;
    uint8_t flag_str;
    uint8_t type;
    uint8_t autorun;
    uint8_t end_addr_hi;
    uint8_t end_addr_lo;
    uint8_t start_addr_hi;
    uint8_t start_addr_lo;
    uint8_t reserved;
    uint8_t filename[16];
} LociTapHdr;

// Directory stream handle
typedef struct
{
    int16_t  fd;
    uint16_t off;
    char     name[64];
} LociDir;

// Directory entry (72 bytes) — must match CC65 struct dirent layout exactly:
//   int(2) + char[64] + uint8(1) + uint8(1) + ulong(4) = 72
// MIA_OP_READDIR pops exactly sizeof(LociDirent) bytes from XSTACK.
typedef struct
{
    int16_t  d_fd;
    char     d_name[64];
    uint8_t  d_attrib;
    uint8_t  reserved;
    uint32_t d_size;
} LociDirent;

#define LOCI_DIRENT_SIZE  72

// LOCI firmware version
typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} LociVersion;

// uname result — must match CC65 struct utsname exactly (69 bytes total).
// MIA_OP_UNAME pops exactly sizeof(struct utsname) bytes from XSTACK.
// Field sizes from cc65/include/sys/utsname.h: sysname[17]+nodename[9]+
//   release[9]+version[9]+machine[25] = 69 bytes.
// release holds firmware version string e.g. "1.2.3".
typedef struct
{
    char sysname[17];
    char nodename[9];
    char release[9];    // firmware version e.g. "1.2.3"
    char version[9];
    char machine[25];
} LociUname;  // 69 bytes

// Storage configuration (populated by get_locicfg)
#define MAXDEV  9
typedef struct
{
    uint8_t     devnr;
    uint8_t     validdev[MAXDEV + 1];
    LociVersion version;
    LociUname   uname;
} LociCfg;

// XRAM copy buffer address and size (in extended RAM at $8000+)
#define COPYBUF_XRAM_ADDR  ((uint16_t)0x8000)
#define COPYBUF_XRAM_SIZE  ((uint16_t)0x0800)

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────

extern uint8_t  loci_errno;
extern LociCfg  locicfg;

// loci_errno value for "not empty" returned by MIA_OP_UNLINK on a
// non-empty directory (FatFs FR_DENIED / POSIX ENOTEMPTY, mapped to
// EACCES by Phosphoric's loci_fs.c op_unlink -- confirmed via that
// emulation; revisit against real LOCI firmware if it differs).
#define LOCI_EACCES 3

// ─────────────────────────────────────────────────────────────────────────────
// MIA helpers — macros and function prototypes
//
// Calling convention (matches CC65 v1 mia.s byte ordering):
//   Push int16: high byte first to XSTACK, then low byte.
//   Pop int16:  low byte from XSTACK, then high byte → hi<<8|lo.
//   Push int32: byte3(MSB) first, then byte2, byte1, byte0(LSB).
//   Set areg/xreg:  MIA.xreg = high byte; MIA.areg = low byte.
//   Call (mia_call_int/mia_call_long): write opcode to MIA.op, CLV, then
//     JSR MIA.spin ($03B1) — must be EXECUTED, not polled. The firmware
//     writes a tiny 6502 routine into $03B0-$03B9: while busy it is a
//     "CLV;BVC -2" self-loop; once done it is "CLV;BVC+0;LDA #lo;LDX
//     #hi;RTS" (result in A/X, mia_call_long also reads MIA.sreg for the
//     upper 16 bits). CLV before the JSR is required because $03B1 is the
//     BVC opcode itself (the firmware's own CLV sits one byte earlier, at
//     $03B0) — the busy-loop's correctness depends on V being clear on
//     entry.
//   Call (mia_call_boot, MIA_OP_BOOT only): the "done" sequence is instead
//     "CLV;BVC+0;JMP ($FFFC)" — a jump to the reset vector that never
//     returns on success. Combining trigger+JSR like mia_call_int hangs on
//     real LOCI hardware for this op, so mia_call_boot splits it: set
//     MIA.op, poll MIA.busy as plain data until done, clear VIA.ifr/VIA.ier
//     (a stale unacknowledged Timer 1 IFR flag would otherwise fire an IRQ
//     the instant the booted ROM's cold-start enables IER and executes CLI
//     — v2 runs permanently under SEI with no IRQ handler), then JSR
//     MIA.spin to take the jump. On failure it returns the LDA/LDX result
//     like mia_call_int.
//   Result int16: MIA.xreg<<8 | MIA.areg
//   Result int32: also reads MIA.sreg for upper 16 bits.
//   Set areg/xreg/sreg (mia_set_axsreg): for ops whose single int32
//     argument is passed in registers, not XSTACK (e.g. MIA_OP_TAP_SEEK
//     reads API_AXSREG directly) — MIA.sreg = upper 16 bits, then
//     mia_set_ax() for the lower 16 bits.
//   Error:      result < 0 → loci_errno = MIA.errno_lo, return -1.
// ─────────────────────────────────────────────────────────────────────────────

// Single-byte XSTACK push/pop — macros to avoid function call overhead
#define mia_push_char(v)   (MIA.xstack = (uint8_t)(v))
#define mia_pop_char()     (MIA.xstack)

// Multi-byte helpers — implemented in loci.c; auto-compiled via pragma below
void     mia_push_int(uint16_t v);
int16_t  mia_pop_int(void);
void     mia_push_long(uint32_t v);
uint32_t mia_pop_long(void);
void     mia_set_ax(uint16_t v);
void     mia_set_axsreg(uint32_t v);
int16_t  mia_call_int(uint8_t op);
int16_t  mia_call_int_errno(uint8_t op);
int32_t  mia_call_long(uint8_t op);
int32_t  mia_call_long_errno(uint8_t op);
int16_t  mia_call_boot(uint8_t settings);

// ─────────────────────────────────────────────────────────────────────────────
// Function prototypes
// ─────────────────────────────────────────────────────────────────────────────

// --- Detection & config ---
bool        loci_present(void);
void        loci_uname(LociUname *buf);
void        get_locicfg(void);
bool        loci_check_fw(uint8_t major, uint8_t minor, uint8_t patch);
const char *get_loci_devname(uint8_t devid, uint8_t maxlength);

// --- System ---
int16_t phi2(void);
int32_t loci_lrand(void);

// --- XRAM access ---
void    xram_poke(uint16_t addr, uint8_t val);
uint8_t xram_peek(uint16_t addr);
void    xram_memcpy_to(uint16_t dest, const void *src, uint16_t count);
void    xram_memcpy_from(void *dest, uint16_t src, uint16_t count);

// --- Overlay RAM ---
void enable_overlay_ram(void);
void disable_overlay_ram(void);

// --- File I/O ---
int16_t loci_open(const char *path, uint16_t flags);
int16_t loci_close(int16_t fd);
int16_t loci_read(int16_t fd, void *buf, uint16_t count);
int16_t loci_write(int16_t fd, const void *buf, uint16_t count);
int32_t loci_lseek(int16_t fd, int32_t offset, uint8_t whence);
int16_t loci_unlink(const char *path);
int16_t loci_rename(const char *oldpath, const char *newpath);

// --- High-level file ops ---
bool    file_exists(const char *path);
int16_t file_load(const char *path, void *dst, uint16_t count);
int16_t file_save(const char *path, const void *src, uint16_t count);
int16_t file_copy(const char *dst, const char *src);
int16_t file_copy_progress(const char *dst, const char *src,
                            uint8_t progx, uint8_t progy, uint8_t progl);

// --- Directory ops ---
LociDir    *loci_opendir(const char *path);
void        loci_closedir(LociDir *dir);
LociDirent *loci_readdir(LociDir *dir);
int16_t     loci_mkdir(const char *path);
void        loci_getcwd(char *buf, uint8_t len);

// --- Mount ops ---
int16_t loci_mount(int16_t drive, const char *path, const char *filename);
int16_t loci_umount(int16_t drive);

// --- Tape ops ---
int32_t tap_seek(int32_t pos);
int32_t tap_tell(void);
int32_t tap_read_header(LociTapHdr *hdr);

// Auto-compile loci.c when this header is included (matches charwin.h pattern)
#pragma compile("loci.c")

#endif
