// loci.c - LOCI mass-storage device library for Oscar64 / Oric Atmos (bare-metal)
//
// Based on:
//   LOCI ROM by Sodiumlightbaby, 2024  https://github.com/sodiumlb/loci-rom
//   Picocomputer 6502 by Rumbledethumps, 2023  https://github.com/picocomputer/rp6502
//   locifilemanager v1 (CC65) by Xander Mol, 2025
//     libsrc/mia.s, open.c, read_xstack.c, opendir.c, readdir.c,
//     fileops.c, mount.c, xram_memcpy.s, ijk-driver.s, getstoragecfg.c
//
// Adapted: Oscar64 native mode (-n); C functions replace assembly helpers;
// stdint types throughout; SEI/CLI via __asm for IRQ-safe sections.

#include "loci.h"
#include "charwin.h"
#include "strings.h"

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────

uint8_t loci_errno = 0;
LociCfg locicfg;

// ─────────────────────────────────────────────────────────────────────────────
// MIA helper implementations
//
// Calling convention (matches CC65 v1 mia.s byte ordering — see loci.h header).
// Field names MIA.areg / MIA.xreg used instead of MIA.a / MIA.x because
// Oscar64 native mode (-n) treats 'a' and 'x' as 6502 register keywords.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Push a 16-bit value onto XSTACK, high byte first then low byte (matches
 * the CC65 v1 mia.s push-int byte ordering).
 *
 * @param v Value to push.
 * @return (none)
 */
void mia_push_int(uint16_t v)
{
    MIA.xstack = (uint8_t)(v >> 8);
    MIA.xstack = (uint8_t)v;
}

/**
 * Pop a 16-bit value from XSTACK, low byte first then high byte.
 *
 * @return The popped 16-bit value, reassembled as hi<<8 | lo.
 */
int16_t mia_pop_int(void)
{
    uint8_t lo = MIA.xstack;
    uint8_t hi = MIA.xstack;
    return (int16_t)((uint16_t)hi << 8 | lo);
}

/**
 * Push a 32-bit value onto XSTACK, most-significant byte first down to
 * least-significant byte last.
 *
 * @param v Value to push.
 * @return (none)
 */
void mia_push_long(uint32_t v)
{
    MIA.xstack = (uint8_t)(v >> 24);
    MIA.xstack = (uint8_t)(v >> 16);
    MIA.xstack = (uint8_t)(v >> 8);
    MIA.xstack = (uint8_t)v;
}

/**
 * Pop a 32-bit value from XSTACK, least-significant byte first.
 *
 * @return The popped 32-bit value, reassembled from the four bytes.
 */
uint32_t mia_pop_long(void)
{
    uint8_t b0 = MIA.xstack;
    uint8_t b1 = MIA.xstack;
    uint8_t b2 = MIA.xstack;
    uint8_t b3 = MIA.xstack;
    return ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | (uint32_t)b0;
}

/**
 * Set MIA.areg/MIA.xreg from a 16-bit value (areg = low byte, xreg = high
 * byte), for ops that take their argument via the A/X registers.
 *
 * @param v Value to split into MIA.areg (low byte) and MIA.xreg (high byte).
 * @return (none)
 */
void mia_set_ax(uint16_t v)
{
    MIA.xreg = (uint8_t)(v >> 8);
    MIA.areg = (uint8_t)v;
}

/**
 * Set MIA.sreg/MIA.areg/MIA.xreg from a 32-bit value, for ops whose single
 * int32 argument is passed in registers rather than via XSTACK (e.g.
 * MIA_OP_TAP_SEEK).
 *
 * @param v Value to split: upper 16 bits into MIA.sreg, lower 16 bits into
 *          MIA.areg/MIA.xreg via mia_set_ax().
 * @return (none)
 */
void mia_set_axsreg(uint32_t v)
{
    MIA.sreg = (uint16_t)(v >> 16);
    mia_set_ax((uint16_t)v);
}

// MIA_OP_BOOT (with the FAST bit boot() always sets) ends its MIA.spin
// sequence with api_return_boot()'s "CLV; BVC+0; JMP ($FFFC)" instead of
// the normal "CLV; BVC+0; LDA #lo; LDX #hi; RTS" — that JMP to the 6502
// reset vector IS the reboot. Polling MIA.busy and reading MIA.areg/xreg
// as plain data (the previous implementation) never executes it, so boot
// silently never reboots, and the $FC/$FF JMP-operand bytes misread as
// AX=$FFFC (-4), a false error. JSR MIA.spin ($03B1) executes the
// firmware-written sequence directly: it blocks via an in-place BVC loop
// while busy, then either returns with the result in A/X (normal ops) or
// jumps into the freshly booted ROM and never returns (MIA_OP_BOOT
// success). Matches v1 mia.s _mia_call_int (sta MIA_OP; jmp MIA_SPIN).
//
// CLV before the JSR is required: $03B1 is the BVC opcode itself (the
// CLV the firmware wrote sits at $03B0, one byte EARLIER) so JSR 0x03b1
// enters past it, at the mercy of whatever the V flag happens to be from
// preceding code. If V is set on entry, "BVC -2" (the busy pattern) does
// NOT branch on the first check and falls straight through to the stale
// LDA #areg/LDX #xreg/RTS bytes — returning immediately with garbage
// before the firmware has even started, right as it begins overwriting
// low RAM for the boot. At -O2 a preceding BIT-style flag test can leave
// V set depending on the call site, so this can't be left to chance.
/**
 * Invoke MIA operation op via JSR MIA.spin (see comment above) and return
 * its int16 result from MIA.areg/MIA.xreg.
 *
 * @param op MIA_OP_* operation code to write to MIA.op.
 * @return int16 result (MIA.xreg<<8 | MIA.areg) from the firmware.
 */
int16_t mia_call_int(uint8_t op)
{
    return __asm {
        lda op
        sta [0x03af]
        clv
        jsr 0x03b1
        sta accu
        stx accu + 1
    };
}

/**
 * Like mia_call_int(), but on a negative result capture MIA.errno_lo into
 * loci_errno and normalize the return value to -1.
 *
 * @param op MIA_OP_* operation code to write to MIA.op.
 * @return int16 result from mia_call_int(op), or -1 with loci_errno set on
 *         error.
 */
int16_t mia_call_int_errno(uint8_t op)
{
    int16_t r = mia_call_int(op);
    if (r < 0) { loci_errno = MIA.errno_lo; return -1; }
    return r;
}

// MIA_OP_BOOT only. mia_call_int's "sta [0x03af]; jsr 0x03b1" — writing
// the op and immediately JSR-ing into the busy/done dispatch at $03B1 —
// hangs on real LOCI hardware for this op specifically (confirmed: same
// settings via mia_call_int_errno(MIA_OP_BOOT) freeze with no further
// code executing, while the sequence below completes and reboots
// correctly for both ESC-exit ($80) and TAP-launch ($92) settings).
// Splitting the wait from the jump avoids whatever the hazard is: poll
// MIA.busy as plain data until the firmware finishes (success rewrites
// $03B0-$03B7 to "CLV;BVC+0;JMP($FFFC)"; failure rewrites it to the
// normal "CLV;BVC+0;LDA#lo;LDX#hi;RTS" released form), THEN jsr 0x03b1.
// On success that JMP reboots the machine and this never returns; on
// failure it returns the LDA/LDX result like mia_call_int.
/**
 * Invoke MIA_OP_BOOT with the given settings byte (see comment above for the
 * split poll/JSR sequence and the VIA.ier/VIA.ifr reset). On success this
 * jumps into the freshly-booted ROM via JMP ($FFFC) and never returns; on
 * failure captures MIA.errno_lo into loci_errno and returns -1.
 *
 * @param settings Boot flag byte (bit 7 = FAST, plus ald/bit/b11/tap/fdc
 *                  mount-status bits as built by main.c's boot()).
 * @return int16 result from the firmware on failure (normalized to -1 with
 *         loci_errno set), or does not return on success.
 */
int16_t mia_call_boot(uint8_t settings)
{
    int16_t r;

    mia_set_ax((uint16_t)settings);
    MIA.op = MIA_OP_BOOT;

    while (MIA.busy & MIA_BUSY_BIT) { }

    // On success the jump below lands in the freshly-loaded ROM's cold-start
    // (e.g. BASIC10 $F84A), which performs its own VIA init and eventually
    // executes CLI. v2 runs permanently under SEI with no IRQ handler, so
    // VIA.ifr can accumulate a stale, unacknowledged Timer 1 flag (set once,
    // ~10ms after startup, and never cleared since nothing ever reads T1C-L
    // or rewrites T1C-H). If the cold-start enables VIA.ier's T1 bit before
    // it clears/reads T1C-L, that stale IFR bit fires an IRQ the instant it
    // executes CLI -- before its own zero-page setup is complete -- hanging
    // the machine. v1 avoided this by running with IRQs enabled throughout
    // (IFR serviced every jiffy) and writing VIA.ier = 0x7F just before this
    // same call. Here we both disable IER and clear IFR, leaving VIA in a
    // clean, reset-like state for the booted ROM to initialise from.
    VIA.ier = 0x7F;
    VIA.ifr = 0x7F;

    r = __asm {
        jsr 0x03b1
        sta accu
        stx accu + 1
    };

    if (r < 0) { loci_errno = MIA.errno_lo; return -1; }
    return r;
}

// JSR-based, like mia_call_int (see its comment) — executes the firmware's
// busy/done routine at MIA.spin rather than reading MIA.areg/xreg/sreg as
// plain data. Matches v1 mia.s _mia_call_long (sta MIA_OP; jsr MIA_SPIN;
// ldy MIA_SREG; ...). None of mia_call_long's ops end in a JMP-style
// return, so A/X come back as the normal LDA #lo / LDX #hi result.
//
// CLV before the JSR — see mia_call_int's comment: $03B1 is the BVC
// opcode itself, one byte past the firmware's own CLV at $03B0, so the
// busy-loop's correctness depends on V being clear on entry.
/**
 * Invoke MIA operation op via JSR MIA.spin (see comment above) and return
 * its int32 result from MIA.areg/MIA.xreg/MIA.sreg.
 *
 * @param op MIA_OP_* operation code to write to MIA.op.
 * @return int32 result (MIA.sreg<<16 | MIA.xreg<<8 | MIA.areg) from the
 *         firmware.
 */
int32_t mia_call_long(uint8_t op)
{
    return __asm {
        lda op
        sta [0x03af]
        clv
        jsr 0x03b1
        sta accu
        stx accu + 1
        lda [0x03b8]
        sta accu + 2
        lda [0x03b9]
        sta accu + 3
    };
}

/**
 * Like mia_call_long(), but on a negative result capture MIA.errno_lo into
 * loci_errno and normalize the return value to -1.
 *
 * @param op MIA_OP_* operation code to write to MIA.op.
 * @return int32 result from mia_call_long(op), or -1 with loci_errno set on
 *         error.
 */
int32_t mia_call_long_errno(uint8_t op)
{
    int32_t r = mia_call_long(op);
    if (r < 0) { loci_errno = MIA.errno_lo; return -1; }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers (static — not part of the public API)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Push a NUL-terminated string to XSTACK in reverse order (last char first).
 * The MIA firmware reverses on dequeue, presenting bytes in forward order.
 *
 * @param s NUL-terminated string to push (e.g. a path argument).
 * @return (none)
 */
static void push_path(const char *s)
{
    uint8_t len = 0;
    while (s[len]) len++;
    while (len) mia_push_char((uint8_t)s[--len]);
}

/**
 * Read up to count bytes from an open file to buf via XSTACK
 * (MIA_OP_READ_XSTACK).
 *
 * @param buf    Destination buffer (at least count bytes).
 * @param count  Number of bytes to read (<= 256).
 * @param fildes Open LOCI file descriptor.
 * @return Number of bytes actually read (>= 0), or -1 with loci_errno set on
 *         error.
 */
static int16_t read_xstack(void *buf, uint16_t count, int16_t fildes)
{
    int16_t got;
    mia_push_int(count);
    mia_set_ax((uint16_t)fildes);
    got = mia_call_int_errno(MIA_OP_READ_XSTACK);
    if (got > 0)
    {
        uint8_t *bbuf = (uint8_t *)buf;
        int16_t i;
        for (i = 0; i < got; i++)
        {
            uint8_t ch = MIA.xstack;
            bbuf[i] = ch;
        }
    }
    return got;
}

/**
 * Write up to count bytes from buf to an open file via XSTACK
 * (MIA_OP_WRITE_XSTACK). See the comment below for why count is not itself
 * pushed onto XSTACK.
 *
 * @param buf    Source buffer (count bytes).
 * @param count  Number of bytes to write (<= 256).
 * @param fildes Open LOCI file descriptor.
 * @return Number of bytes actually written (>= 0), or -1 with loci_errno set
 *         on error.
 */
// Unlike read_xstack(), count is NOT pushed onto XSTACK -- matches v1's
// write_xstack.c (libsrc/write_xstack.c) and the real firmware's
// std_api_write_xstack (sodiumlb/loci-firmware src/mia/api/std.c), which
// derives the byte count from XSTACK_SIZE - xstack_ptr (i.e. how many bytes
// are currently pushed), not from an explicit count value.
static int16_t write_xstack(const void *buf, uint16_t count, int16_t fildes)
{
    uint16_t i = count;
    while (i) mia_push_char(((const uint8_t *)buf)[--i]);
    mia_set_ax((uint16_t)fildes);
    return mia_call_int_errno(MIA_OP_WRITE_XSTACK);
}

/**
 * Read count bytes from an open file into XRAM at xram_addr
 * (MIA_OP_READ_XRAM). See the comment below for the XSTACK argument-passing
 * protocol.
 *
 * @param xram_addr XRAM destination address.
 * @param count     Number of bytes to read.
 * @param fildes    Open LOCI file descriptor.
 * @return Number of bytes actually read (>= 0), or -1 with loci_errno set on
 *         error.
 */
// xram_addr and count are passed via XSTACK (buf pushed first, then count
// — MIA_OP_READ_XRAM pops count first, then xram_addr), matching v1's
// read_xram() (libsrc/read_xram.c). MIA.addr0/.step0/.rw0 are a *different*
// direct register DMA window (used by xram_memcpy_*()/xram_peek/poke) and
// are not consulted by this op — writing MIA.addr0 here left only 2 bytes
// on XSTACK, one short of the 4 MIA_OP_READ_XRAM expects, so the firmware
// returned EINVAL on every call.
static int16_t read_xram(uint16_t xram_addr, uint16_t count, int16_t fildes)
{
    mia_push_int(xram_addr);
    mia_push_int(count);
    mia_set_ax((uint16_t)fildes);
    return mia_call_int_errno(MIA_OP_READ_XRAM);
}

/**
 * Write count bytes from XRAM at xram_addr to an open file
 * (MIA_OP_WRITE_XRAM). See read_xram() for why xram_addr/count are pushed
 * via XSTACK rather than MIA.addr1.
 *
 * @param xram_addr XRAM source address.
 * @param count     Number of bytes to write.
 * @param fildes    Open LOCI file descriptor.
 * @return Number of bytes actually written (>= 0), or -1 with loci_errno set
 *         on error.
 */
static int16_t write_xram(uint16_t xram_addr, uint16_t count, int16_t fildes)
{
    mia_push_int(xram_addr);
    mia_push_int(count);
    mia_set_ax((uint16_t)fildes);
    return mia_call_int_errno(MIA_OP_WRITE_XRAM);
}

// ─────────────────────────────────────────────────────────────────────────────
// Detection & configuration
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Check whether the LOCI device is present by reading its identity marker.
 *
 * @return true if the LOCI ROM has written 'L' at LOCI_SIGNATURE_ADDR
 *         ($0319), false otherwise.
 */
bool loci_present(void)
{
    return *LOCI_SIGNATURE_ADDR == 'L';
}

/**
 * Populate *buf with the LOCI firmware's uname information (sysname,
 * nodename, release, version, machine), read byte-by-byte from XSTACK after
 * MIA_OP_UNAME (firmware pushes exactly sizeof(LociUname) == 69 bytes).
 *
 * @param buf Destination LociUname struct (69 bytes).
 * @return (none)
 */
// Based on sysuname.c in sodiumlb/loci-rom (XSTACK loop, not DMA).
void loci_uname(LociUname *buf)
{
    uint8_t  i;
    uint8_t *p = (uint8_t *)buf;
    mia_call_int(MIA_OP_UNAME);
    for (i = 0; i < (uint8_t)sizeof(LociUname); i++)
    {
        uint8_t ch = MIA.xstack;
        p[i] = ch;
    }
}

/**
 * Populate the global locicfg with firmware version info (parsed from
 * loci_uname()'s release string) and enumerate mounted USB mass-storage
 * devices by walking the root directory for "N.MSC.*" entries, setting
 * locicfg.validdev[] and locicfg.devnr accordingly (drive 0 is always
 * valid). If LOCI is not present, shows MSG_LOCI_NOT_FOUND and halts the
 * machine (bare-metal, no exit()).
 *
 * @return (none) -- result is written to the global locicfg; does not return
 *         if LOCI is absent.
 */
void get_locicfg(void)
{
    uint8_t     devid;
    LociDir    *dir;
    LociDirent *fil;

    if (!loci_present())
    {
        OricCharWin err;
        cwin_init(&err, 2, 12, 38, 2, A_FWRED, A_BGBLACK);
        cwin_clear(&err);
        cwin_putat_string(&err, 0, 0, MSG_LOCI_NOT_FOUND);
        cwin_putat_string(&err, 0, 1, MSG_PRESS_KEY_EXIT);
        cwin_getch();
        while (1) {}    // bare-metal halt (no exit() on Oric)
    }

    // Zero-fill config struct
    {
        uint8_t *p = (uint8_t *)&locicfg;
        uint8_t  i;
        for (i = 0; i < sizeof(locicfg); i++) p[i] = 0;
    }

    // Get firmware version via uname release string (e.g. "1.2.3" or "1.2.34")
    loci_uname(&locicfg.uname);
    {
        const char *rel = locicfg.uname.release;
        locicfg.version.major = (uint8_t)(rel[0] - '0');
        locicfg.version.minor = (uint8_t)(rel[2] - '0');
        locicfg.version.patch = (uint8_t)(rel[4] - '0');
        if (rel[5] && rel[5] != '\0')
            locicfg.version.patch = (uint8_t)(locicfg.version.patch * 10 + (rel[5] - '0'));
    }

    // Drive 0 always valid
    locicfg.validdev[0] = 1;

    // Walk root dir to enumerate USB MSC devices (entries "N.MSC.*")
    dir = loci_opendir("");
    if (!dir) return;
    while (1)
    {
        fil = loci_readdir(dir);
        if (!fil || fil->d_name[0] == '\0') break;
        if (locicfg.devnr >= MAXDEV) break;

        devid = (uint8_t)(fil->d_name[0] - '0');
        if (devid && fil->d_name[3] == 'M' && fil->d_name[4] == 'S' && fil->d_name[5] == 'C')
        {
            locicfg.devnr++;
            locicfg.validdev[devid] = 1;
        }
    }
    loci_closedir(dir);
}

/**
 * Check whether the LOCI firmware version recorded in locicfg.version is at
 * least major.minor.patch (lexicographic major/minor/patch comparison).
 *
 * @param major Minimum required major version.
 * @param minor Minimum required minor version (only checked if major
 *               matches).
 * @param patch Minimum required patch version (only checked if major and
 *               minor both match).
 * @return true if locicfg.version >= major.minor.patch, false otherwise.
 */
bool loci_check_fw(uint8_t major, uint8_t minor, uint8_t patch)
{
    if (locicfg.version.major > major) return true;
    if (locicfg.version.major == major && locicfg.version.minor > minor) return true;
    if (locicfg.version.major == major && locicfg.version.minor == minor &&
        locicfg.version.patch >= patch) return true;
    return false;
}

/**
 * Look up the directory entry name of the devid-th root-directory entry
 * (0-based) and return its device label, truncated to maxlength characters.
 * The label starts at d_name[3] (after the "N.M" device-number prefix). The
 * result is copied into a static buffer so it survives the directory close.
 *
 * @param devid     0-based index of the root-directory entry to look up.
 * @param maxlength Maximum number of label characters to return (label is
 *                   truncated in place if longer).
 * @return Pointer to a static, NUL-terminated device label string, or "" if
 *         devid is out of range.
 */
const char *get_loci_devname(uint8_t devid, uint8_t maxlength)
{
    static LociDirent entry;
    LociDir    *dir;
    LociDirent *fil = 0;
    uint8_t     i;

    dir = loci_opendir("");
    for (i = 0; i <= devid; i++)
        fil = loci_readdir(dir);
    loci_closedir(dir);

    if (!fil) return "";

    // Copy to static entry so result survives the dir close
    for (i = 0; i < 64; i++) entry.d_name[i] = fil->d_name[i];

    // Truncate at maxlength (name starts at d_name[3])
    {
        char   *name = entry.d_name + 3;
        uint8_t len  = 0;
        while (name[len]) len++;
        if (len > maxlength) name[maxlength] = '\0';
        return name;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// System
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Query the CPU clock speed via MIA_OP_PHI2.
 *
 * @return CPU clock speed in kHz.
 */
int16_t phi2(void)
{
    return mia_call_int(MIA_OP_PHI2);
}

/**
 * Generate a random 32-bit number via MIA_OP_LRAND.
 *
 * @return A pseudo-random int32 value from the firmware.
 */
int32_t loci_lrand(void)
{
    return mia_call_long(MIA_OP_LRAND);
}

// ─────────────────────────────────────────────────────────────────────────────
// XRAM direct access
//
// Protocol (from libsrc/xram_memcpy.s):
//   step0 = 1 (auto-increment address after each byte)
//   addr0 = target/source XRAM address
//   Writes: store bytes to rw0; spin after last byte
//   Reads:  load bytes from rw0 (synchronous, no spin needed)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Write a single byte to XRAM at addr via the direct DMA register window
 * (MIA.step0/MIA.addr0/MIA.rw0), waiting for the operation to complete.
 *
 * @param addr XRAM address to write.
 * @param val  Byte value to write.
 * @return (none)
 */
void xram_poke(uint16_t addr, uint8_t val)
{
    MIA.step0 = 1;
    MIA.addr0 = addr;
    MIA.rw0   = val;
    while (MIA.busy & MIA_BUSY_BIT) {}
}

/**
 * Read a single byte from XRAM at addr via the direct DMA register window
 * (MIA.step0/MIA.addr0/MIA.rw0). The read is synchronous; no busy-wait is
 * needed.
 *
 * @param addr XRAM address to read.
 * @return The byte value read from XRAM.
 */
uint8_t xram_peek(uint16_t addr)
{
    MIA.step0 = 1;
    MIA.addr0 = addr;
    return MIA.rw0;
}

/**
 * Copy count bytes from local RAM at src to XRAM at dest via the direct DMA
 * register window (MIA.step0/MIA.addr0/MIA.rw0, auto-incrementing), waiting
 * for the operation to complete.
 *
 * @param dest  XRAM destination address.
 * @param src   Local source buffer (count bytes).
 * @param count Number of bytes to copy.
 * @return (none)
 */
void xram_memcpy_to(uint16_t dest, const void *src, uint16_t count)
{
    const uint8_t *p = (const uint8_t *)src;
    uint16_t       i;
    MIA.step0 = 1;
    MIA.addr0 = dest;
    for (i = 0; i < count; i++)
        MIA.rw0 = p[i];
    while (MIA.busy & MIA_BUSY_BIT) {}
}

/**
 * Copy count bytes from XRAM at src to local RAM at dest via the direct DMA
 * register window (MIA.step0/MIA.addr0/MIA.rw0, auto-incrementing); each
 * read is synchronous.
 *
 * @param dest  Local destination buffer (count bytes).
 * @param src   XRAM source address.
 * @param count Number of bytes to copy.
 * @return (none)
 */
void xram_memcpy_from(void *dest, uint16_t src, uint16_t count)
{
    uint8_t  *p = (uint8_t *)dest;
    uint16_t  i;
    MIA.step0 = 1;
    MIA.addr0 = src;
    for (i = 0; i < count; i++)
        p[i] = MIA.rw0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay RAM ($C000–$FFFF via MICRODISCCFG $0314)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Map overlay RAM ($C000-$FFFF) into the address space via MICRODISCCFG
 * ($0314), exposing LOCI's MIA/TAP register blocks and hiding the BASIC ROM.
 *
 * @return (none) -- writes MICRODISCCFG.
 */
void enable_overlay_ram(void)  { MICRODISCCFG = 0xFD; }

/**
 * Unmap overlay RAM, restoring the normal ROM/I/O view at $C000-$FFFF via
 * MICRODISCCFG ($0314).
 *
 * @return (none) -- writes MICRODISCCFG.
 */
void disable_overlay_ram(void) { MICRODISCCFG = 0xFF; }

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Open a file at path with the given flags (O_RDONLY/O_WRONLY/O_RDWR,
 * optionally combined with O_CREAT/O_TRUNC/O_APPEND/O_EXCL) via MIA_OP_OPEN.
 *
 * @param path  Drive-prefixed path of the file to open.
 * @param flags Bitwise combination of O_* flags (see loci.h).
 * @return Non-negative file descriptor on success, or a negative LOCI_E*
 *         error code on failure.
 */
int16_t loci_open(const char *path, uint16_t flags)
{
    push_path(path);
    mia_set_ax(flags);
    return mia_call_int_errno(MIA_OP_OPEN);
}

/**
 * Close an open file descriptor via MIA_OP_CLOSE.
 *
 * @param fd File descriptor previously returned by loci_open()/loci_opendir().
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_close(int16_t fd)
{
    mia_set_ax((uint16_t)fd);
    return mia_call_int_errno(MIA_OP_CLOSE);
}

/**
 * Read up to count bytes from fd into buf, splitting the transfer into
 * <=256-byte blocks (the XSTACK protocol's per-call limit, see
 * read_xstack()) and stopping early if a short read indicates EOF.
 *
 * @param fd    Open file descriptor to read from.
 * @param buf   Destination buffer (count bytes).
 * @param count Number of bytes requested.
 * @return Total number of bytes actually read (may be less than count at
 *         EOF), or a negative LOCI_E* error code on failure.
 */
int16_t loci_read(int16_t fd, void *buf, uint16_t count)
{
    int16_t  total = 0;
    uint8_t *p     = (uint8_t *)buf;
    while (count)
    {
        uint16_t block = (count > 256) ? 256 : count;
        int16_t  got   = read_xstack(p + total, block, fd);
        if (got < 0) return got;
        total = (int16_t)(total + got);
        count = (uint16_t)(count - (uint16_t)got);
        if ((uint16_t)got < block) break;
    }
    return total;
}

/**
 * Write up to count bytes from buf to fd, splitting the transfer into
 * <=256-byte blocks (the XSTACK protocol's per-call limit, see
 * write_xstack()) and stopping early if a short write is reported.
 *
 * @param fd    Open file descriptor to write to.
 * @param buf   Source buffer (count bytes).
 * @param count Number of bytes to write.
 * @return Total number of bytes actually written, or a negative LOCI_E*
 *         error code on failure.
 */
int16_t loci_write(int16_t fd, const void *buf, uint16_t count)
{
    int16_t        total = 0;
    const uint8_t *p     = (const uint8_t *)buf;
    while (count)
    {
        uint16_t block = (count > 256) ? 256 : count;
        int16_t  put   = write_xstack(p + total, block, fd);
        if (put < 0) return put;
        total = (int16_t)(total + put);
        count = (uint16_t)(count - (uint16_t)put);
        if ((uint16_t)put < block) break;
    }
    return total;
}

/**
 * Reposition the file offset of fd via MIA_OP_LSEEK.
 *
 * @param fd     Open file descriptor to reposition.
 * @param offset Offset in bytes, interpreted according to whence.
 * @param whence One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The resulting absolute file offset, or a negative LOCI_E* error
 *         code on failure.
 */
int32_t loci_lseek(int16_t fd, int32_t offset, uint8_t whence)
{
    mia_push_long((uint32_t)offset);
    mia_push_char(whence);
    mia_set_ax((uint16_t)fd);
    return mia_call_long_errno(MIA_OP_LSEEK);
}

/**
 * Delete the file (or empty directory, on the hostfs backend) at path via
 * MIA_OP_UNLINK.
 *
 * @param path Drive-prefixed path of the file/directory to delete.
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_unlink(const char *path)
{
    push_path(path);
    return mia_call_int_errno(MIA_OP_UNLINK);
}

/**
 * Rename/move oldpath to newpath via MIA_OP_RENAME.
 *
 * @param oldpath Drive-prefixed path of the existing file/directory.
 * @param newpath Drive-prefixed destination path.
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_rename(const char *oldpath, const char *newpath)
{
    // Push old path, then a NUL separator, then new path (each reversed).
    // MIA firmware reverses each segment back to forward order on dequeue.
    // Matches sysrename.c in sodiumlb/loci-rom (libsrc reference client).
    push_path(oldpath);
    mia_push_char(0);
    push_path(newpath);
    return mia_call_int_errno(MIA_OP_RENAME);
}

// ─────────────────────────────────────────────────────────────────────────────
// High-level file operations
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Check whether a file exists at path by attempting to open it read-only
 * and immediately closing it.
 *
 * @param path Drive-prefixed path to check.
 * @return true if the file could be opened (exists), false otherwise.
 */
bool file_exists(const char *path)
{
    int16_t fd = loci_open(path, O_RDONLY | O_EXCL);
    if (fd < 0) return false;
    loci_close(fd);
    return true;
}

/**
 * Open path read-only, read up to count bytes into dst, and close it.
 *
 * @param path  Drive-prefixed path of the file to load.
 * @param dst   Destination buffer (count bytes).
 * @param count Maximum number of bytes to read.
 * @return Number of bytes read, or a negative LOCI_E* error code if the file
 *         could not be opened or the read failed.
 */
int16_t file_load(const char *path, void *dst, uint16_t count)
{
    int16_t fd = loci_open(path, O_RDONLY | O_EXCL);
    int16_t r;
    if (fd < 0) return fd;
    r = loci_read(fd, dst, count);
    loci_close(fd);
    return r;
}

/**
 * Open path write-only (creating it if necessary), write count bytes from
 * src, and close it.
 *
 * @param path  Drive-prefixed path of the file to write.
 * @param src   Source buffer (count bytes).
 * @param count Number of bytes to write.
 * @return Number of bytes written, or a negative LOCI_E* error code if the
 *         file could not be opened or the write failed.
 */
int16_t file_save(const char *path, const void *src, uint16_t count)
{
    int16_t fd = loci_open(path, O_WRONLY | O_CREAT);
    int16_t r;
    if (fd < 0) return fd;
    r = loci_write(fd, src, count);
    loci_close(fd);
    return r;
}

/**
 * Copy src to dst via XRAM-buffered block transfers (read_xram()/
 * write_xram() through the COPYBUF_XRAM_* staging area), without any
 * progress indication.
 *
 * @param dst Drive-prefixed destination path (created/truncated).
 * @param src Drive-prefixed source path (opened read-only).
 * @return 0 on success, or a negative LOCI_E* error code if either file
 *         could not be opened or a read/write failed partway through.
 */
int16_t file_copy(const char *dst, const char *src)
{
    int16_t fd_src, fd_dst;
    int16_t len;
    int16_t result = 0;

    fd_src = loci_open(src, O_RDONLY | O_EXCL);
    if (fd_src < 0) return fd_src;

    fd_dst = loci_open(dst, O_WRONLY | O_CREAT);
    if (fd_dst < 0) { loci_close(fd_src); return fd_dst; }

    do {
        len = read_xram(COPYBUF_XRAM_ADDR, COPYBUF_XRAM_SIZE, fd_src);
        if (len < 0) { result = len; break; }
        if (len > 0)
        {
            int16_t wr = write_xram(COPYBUF_XRAM_ADDR, (uint16_t)len, fd_dst);
            if (wr < 0) { result = wr; break; }
        }
    } while (len == (int16_t)COPYBUF_XRAM_SIZE);

    loci_close(fd_src);
    loci_close(fd_dst);
    return result;
}

// Animated progress-bar characters, cycled every read/write block.
static const uint8_t progressBar[4] = { 48, 53, 93, 95 };

/**
 * Copy src to dst via XRAM-buffered block transfers (as file_copy()), while
 * animating a progress bar at (progx, progy) of length progl characters
 * directly in text VRAM, and polling for ESC to allow mid-copy cancellation.
 *
 * @param dst   Drive-prefixed destination path (created/truncated).
 * @param src   Drive-prefixed source path (opened read-only).
 * @param progx Column of the first progress-bar character.
 * @param progy Row of the progress bar in text VRAM.
 * @param progl Width of the progress bar in characters.
 * @return 0 on success, -2 if cancelled via ESC (the partial destination
 *         file is removed), or a negative LOCI_E* error code if either file
 *         could not be opened or a read/write failed partway through.
 */
// Based on libsrc/fileops.c file_copy() (CC65, prog/progx/progy/progl args) by
// Xander Mol, 2025 — local reference at locifilemanager/libsrc/fileops.c.
// Adapted: no conio gotoxy/cputc/cclear in Oscar64 bare-metal — progress bar
// chars are written directly to TEXTVRAM via a row pointer (same pattern as
// menu.c's MENU_ROW macro).
int16_t file_copy_progress(const char *dst, const char *src,
                            uint8_t progx, uint8_t progy, uint8_t progl)
{
    uint8_t *row = (uint8_t *)((uint16_t)TEXTVRAM + (uint16_t)progy * 40U);
    int16_t  fd_src, fd_dst;
    int16_t  len;
    int16_t  result = 0;
    uint8_t  cnt = 0;
    uint8_t  x;

    fd_src = loci_open(src, O_RDONLY | O_EXCL);
    if (fd_src < 0) return fd_src;

    fd_dst = loci_open(dst, O_WRONLY | O_CREAT);
    if (fd_dst < 0) { loci_close(fd_src); return fd_dst; }

    row[progx] = A_ALT;
    for (x = 0; x < progl; x++) row[progx + 1 + x] = 0x20;

    do {
        if (keyb_check() == KEY_ESC) { result = -2; break; }

        if ((cnt >> 2) > (uint8_t)(progl - 2))
        {
            cnt = 0;
            for (x = 0; x < (uint8_t)(progl - 1); x++)
                row[progx + 1 + x] = 0x20;
        }
        else
        {
            row[progx + 1 + (cnt >> 2)] = progressBar[cnt & 3];
            cnt++;
        }

        len = read_xram(COPYBUF_XRAM_ADDR, COPYBUF_XRAM_SIZE, fd_src);
        if (len < 0) { result = len; break; }
        if (len > 0)
        {
            int16_t wr = write_xram(COPYBUF_XRAM_ADDR, (uint16_t)len, fd_dst);
            if (wr < 0) { result = wr; break; }
        }
    } while (len == (int16_t)COPYBUF_XRAM_SIZE);

    loci_close(fd_src);
    loci_close(fd_dst);

    // -2 = cancelled mid-copy (ESC) -- remove the partial destination file.
    if (result == -2)
        loci_unlink(dst);

    for (x = 0; x < progl; x++) row[progx + x] = 0x20;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory operations
// ─────────────────────────────────────────────────────────────────────────────

static LociDir    s_dir;
static LociDirent s_dirent;

/**
 * Open a directory stream at path via MIA_OP_OPENDIR, returning a static
 * LociDir handle. Only one directory stream can be open at a time (the
 * handle and its read buffer are statically allocated).
 *
 * @param path Drive-prefixed path of the directory to open ("" for the
 *              device root).
 * @return Pointer to the static LociDir handle on success, or NULL if the
 *         directory could not be opened.
 */
LociDir *loci_opendir(const char *path)
{
    int16_t fd;
    uint8_t i = 0;
    // Flush any XSTACK residue from prior operations (e.g. loci_uname)
    // before pushing the path argument so the firmware sees a clean stack.
    mia_call_int(MIA_OP_ZXSTACK);
    push_path(path);
    fd = mia_call_int_errno(MIA_OP_OPENDIR);
    s_dir.fd  = fd;
    s_dir.off = 0;
    while (path[i] && i < 63) { s_dir.name[i] = path[i]; i++; }
    s_dir.name[i] = '\0';
    if (fd < 0) return 0;
    return &s_dir;
}

/**
 * Close a directory stream previously opened with loci_opendir() via
 * MIA_OP_CLOSEDIR.
 *
 * @param dir Directory handle returned by loci_opendir().
 * @return (none)
 */
void loci_closedir(LociDir *dir)
{
    mia_set_ax((uint16_t)dir->fd);
    mia_call_int_errno(MIA_OP_CLOSEDIR);
}

/**
 * Read the next directory entry from dir via MIA_OP_READDIR, popping
 * LOCI_DIRENT_SIZE bytes from XSTACK into a static LociDirent buffer.
 *
 * @param dir Directory handle returned by loci_opendir(); dir->off is
 *             incremented on success.
 * @return Pointer to the static LociDirent on success (an empty d_name
 *         signals end-of-directory), or NULL on error.
 */
LociDirent *loci_readdir(LociDir *dir)
{
    uint8_t i;
    uint8_t *p;
    mia_set_ax((uint16_t)dir->fd);
    if (mia_call_int_errno(MIA_OP_READDIR) < 0) return 0;

    // MIA pops LOCI_DIRENT_SIZE bytes from XSTACK in forward order
    p = (uint8_t *)&s_dirent;
    for (i = 0; i < LOCI_DIRENT_SIZE; i++)
    {
        uint8_t ch = MIA.xstack;
        p[i] = ch;
    }

    dir->off++;
    return &s_dirent;
}

/**
 * Create a directory at path via MIA_OP_MKDIR.
 *
 * @param path Drive-prefixed path of the directory to create.
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_mkdir(const char *path)
{
    push_path(path);
    return mia_call_int_errno(MIA_OP_MKDIR);
}

/**
 * Get the current working directory of the active LOCI device into buf via
 * MIA_OP_GETCWD.
 *
 * @param buf Destination buffer for the NUL-terminated path.
 * @param len Size of buf in bytes; at most len-1 characters are written plus
 *             a NUL terminator.
 * @return (none) -- result is written to buf.
 */
// getcwd protocol (from initcwd.s in sodiumlb/loci-rom): confirmed via
// the proven-working nonworkingcc65 branch's libsrc/getcwd_xram.s, which
// loads correctly on the same real hardware this is failing on -- ax is
// NOT "the caller's buffer length minus one" (this function's own
// previous behaviour, and a real bug found 2026-06-21): the reference
// implementation always passes a hardcoded 255 here regardless of the
// destination buffer's actual size, then separately caps how many bytes
// it copies out to its own buffer length. Passing this codebase's much
// smaller HOMEDIR_MAXLEN-1 (63) instead of 255 is suspected to be why
// MIA_OP_GETCWD was erroring out on real hardware (title screen/help
// screens never finding a usable app.homedir) while the reference
// implementation's identical-firmware call succeeds.
void loci_getcwd(char *buf, uint8_t len)
{
    uint8_t i = 0;
    mia_set_ax(255);
    if (mia_call_int_errno(MIA_OP_GETCWD) < 0)
    {
        // On error, the firmware may not have pushed anything onto
        // XSTACK at all -- don't pop from it (found 2026-06-21: the
        // unconditional pop below was reading whatever stale XSTACK
        // content happened to be left over from an unrelated prior op,
        // not a real empty/terminated path).
        buf[0] = '\0';
        return;
    }
    while (i < (uint8_t)(len - 1))
    {
        uint8_t ch = MIA.xstack;
        buf[i++] = (char)ch;
        if (!ch) return;
    }
    buf[i] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// Mount operations
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Mount a disk image file as the given drive via MIA_OP_MOUNT.
 *
 * @param drive    Drive number to mount onto.
 * @param path     Drive-prefixed path of the directory containing the image
 *                   file.
 * @param filename Name of the image file within path.
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_mount(int16_t drive, const char *path, const char *filename)
{
    mia_set_ax((uint16_t)drive);
    push_path(filename);
    mia_push_char('/');
    push_path(path);
    return mia_call_int_errno(MIA_OP_MOUNT);
}

/**
 * Unmount the disk image currently mounted on drive via MIA_OP_UMOUNT.
 *
 * @param drive Drive number to unmount.
 * @return 0 on success, or a negative LOCI_E* error code on failure.
 */
int16_t loci_umount(int16_t drive)
{
    mia_set_ax((uint16_t)drive);
    return mia_call_int_errno(MIA_OP_UMOUNT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tape operations
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Seek to a byte offset within the mounted tape image via MIA_OP_TAP_SEEK.
 *
 * @param pos Absolute byte offset to seek to.
 * @return The resulting absolute tape position, or a negative LOCI_E* error
 *         code on failure.
 */
int32_t tap_seek(int32_t pos)
{
    // MIA_OP_TAP_SEEK reads its single argument from API_AXSREG (registers),
    // not XSTACK — matches v1 tap.c tap_seek() (mia_set_axsreg).
    mia_set_axsreg((uint32_t)pos);
    return mia_call_long_errno(MIA_OP_TAP_SEEK);
}

/**
 * Get the current byte offset within the mounted tape image via
 * MIA_OP_TAP_TELL.
 *
 * @return The current absolute tape position, or a negative LOCI_E* error
 *         code on failure.
 */
int32_t tap_tell(void)
{
    return mia_call_long_errno(MIA_OP_TAP_TELL);
}

/**
 * Read the tape file header at the current tape position into *hdr via
 * MIA_OP_TAP_HDR.
 *
 * @param hdr Destination for the sizeof(LociTapHdr)-byte tape header.
 * @return The tape position returned by MIA_OP_TAP_HDR (interpretation as
 *         for tap_tell()), or a negative LOCI_E* error code on failure;
 *         *hdr is populated from XSTACK regardless.
 */
// MIA_OP_TAP_HDR pushes sizeof(LociTapHdr) header bytes onto XSTACK
// (regardless of success/failure) — pop them into *hdr, matching v1
// tap.c tap_read_header().
int32_t tap_read_header(LociTapHdr *hdr)
{
    int32_t  pos = mia_call_long_errno(MIA_OP_TAP_HDR);
    uint8_t *h   = (uint8_t *)hdr;
    uint8_t  i;
    for (i = 0; i < sizeof(LociTapHdr); i++)
        h[i] = MIA.xstack;
    return pos;
}

