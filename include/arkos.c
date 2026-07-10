// arkos.c - see arkos.h for attribution, scope, and design rationale.
//
// Ported instruction-by-instruction from akyplayer.s's PLY_AKY_PLAY /
// PLY_AKY_READREGISTERBLOCK and its dependent PLY_AKY_RRB_* labels --
// validated first via a from-scratch Python decode replica run against a
// real .aky file before writing this (same methodology as pt3.c's own
// sample-select bug investigation). ROR/ROL carry semantics are replicated
// exactly (see arkos_ror()/arkos_rol()) rather than hand-summarized, since
// several of akyplayer.s's own branches only make sense once you track the
// actual bit rotated in/out, not just "what the bit means".

#include "arkos.h"
#include "ay.h"
#include "oric.h"
#ifdef STORAGE_FLOPPY
#include "floppy.h"
#else
#include "loci.h"
#endif

static uint8_t arkos_peek(uint16_t addr)
{
    return *(volatile uint8_t *)addr;
}

static uint16_t arkos_peek16(uint16_t addr)
{
    return (uint16_t)arkos_peek(addr) | ((uint16_t)arkos_peek((uint16_t)(addr + 1)) << 8);
}

typedef struct {
    uint16_t track_ptr;   // current read position in this channel's Track stream
    uint16_t rb_ptr;      // current read position in this channel's active RegisterBlock
    uint8_t  rb_wait;     // frames remaining before the next Track triple is read (0 = read now)
} ArkosChannel;

static ArkosChannel arkos_chan[3];
static uint16_t arkos_linker_ptr;
static uint16_t arkos_pattern_counter;
static bool     arkos_loaded;

// Shared PSG registers that persist across ticks until a RegisterBlock
// frame explicitly updates them (mirrors akyplayer.s's own
// PLY_AKY_PSGREGISTER6/11/12/13 static storage -- these are NOT reset
// every tick, only when a frame's own decode touches them).
static uint8_t arkos_psg6, arkos_psg11, arkos_psg12, arkos_psg13;

static uint8_t arkos_ay_shadow[14];

// Writes an AY register only if its value actually changed since last
// tick, mirroring pt3.c's own pt3_ay_write_if_changed() -- skips
// ay_write()'s VIA/PCR write sequence + PHP/SEI/PLP overhead for the
// common case of a register holding steady between ticks. Always updates
// the shadow regardless, so arkos_debug_shadow() stays accurate.
static void arkos_ay_write_if_changed(uint8_t reg, uint8_t value)
{
    if (arkos_ay_shadow[reg] != value)
    {
        ay_write(reg, value);
        arkos_ay_shadow[reg] = value;
    }
}

// -------------------------------------------------------------------------
// RegisterBlock decode -- one channel, one frame.
// -------------------------------------------------------------------------

// Decode-in-progress state for a single channel/frame, threaded through
// the PLY_AKY_RRB_* routines below exactly like the reference's own A
// register + carry flag (see arkos_ror()/arkos_rol()).
typedef struct {
    uint16_t ptr;
    uint8_t  a;
    uint8_t  c;           // carry, 0 or 1
    uint8_t  r7;           // shared mixer accumulator, in/out
    uint8_t  vol_reg;      // this channel's own AY volume register index
    uint8_t  freq_reg;      // this channel's own AY tone-period LSB register index
    bool     wrote_vol;
    uint8_t  vol_value;
    bool     wrote_freq_lsb;
    uint8_t  freq_lsb_value;
    bool     wrote_freq_msb;
    uint8_t  freq_msb_reg;   // target register for the MSB write (usually freq_reg+1)
    uint8_t  freq_msb_value;
    bool     wrote_psg6;
    uint8_t  psg6_value;
    bool     wrote_psg11;
    uint8_t  psg11_value;
    bool     wrote_psg12;
    uint8_t  psg12_value;
    bool     wrote_psg13;
    uint8_t  psg13_value;
    bool     retrig;
} ArkosRB;

static uint8_t arkos_rb_read_byte(ArkosRB *r)
{
    uint8_t v = arkos_peek(r->ptr);
    r->ptr = (uint16_t)(r->ptr + 1);
    return v;
}

static void arkos_rb_ror(ArkosRB *r)
{
    uint8_t new_c = r->a & 1;
    r->a = (uint8_t)((r->a >> 1) | (r->c << 7));
    r->c = new_c;
}

static void arkos_rb_rol(ArkosRB *r)
{
    uint8_t new_c = (uint8_t)((r->a >> 7) & 1);
    r->a = (uint8_t)((r->a << 1) | r->c);
    r->c = new_c;
}

static void arkos_rb_close_tone(ArkosRB *r) { r->r7 |= 0x04; }
static void arkos_rb_open_noise(ArkosRB *r) { r->r7 &= 0xDF; }

static void arkos_rb_send_vol(ArkosRB *r, uint8_t value)
{
    r->wrote_vol = true;
    r->vol_value = value;
}

static void arkos_rb_noninitial_from_byte(ArkosRB *r);

// -------------------------------------------------------------------------
// INITIAL STATE (akyplayer.s lines 855-1236)
//
// Deliberately ONE large function rather than several small ones (each
// dispatch branch was originally its own static function) -- Oscar64's
// __interrupt qualifier rejected the original, more-modular version with
// "error 3035: Function to complex for interrupt" once combined with the
// non-initial tree below, apparently a limit on the total call-graph size
// reachable from an __interrupt entry point rather than any single
// function's own complexity. Collapsing each dispatch tree into one
// function (matching pt3.c's own monolithic pt3_channel_tick() style, which
// never hit this limit) fixed it. Semantics are unchanged from the
// original per-branch functions -- each original "return" after handling
// its own branch is preserved as-is below.
// -------------------------------------------------------------------------

static void arkos_rb_initial(ArkosRB *r)
{
    arkos_rb_ror(r); // line 856
    if (r->c == 0)
    {
        // SOFTONLY_OR_SOFTANDHARD
        arkos_rb_ror(r); // line 1010
        if (r->c == 0)
        {
            // SOFTWAREANDHARDWARE, lines 1111-1235
            arkos_rb_ror(r); // retrig? line 1113
            if (r->c == 1) r->retrig = true;
            arkos_rb_ror(r); // noise? line 1123
            if (r->c == 1)
            {
                r->wrote_psg6 = true;
                r->psg6_value = arkos_rb_read_byte(r);
                arkos_rb_open_noise(r);
            }
            r->wrote_psg13 = true;
            r->psg13_value = (uint8_t)(r->a & 15); // lines 1145-1146
            r->wrote_freq_lsb = true;
            r->freq_lsb_value = arkos_rb_read_byte(r); // line 1158
            r->wrote_freq_msb = true;
            r->freq_msb_reg = (uint8_t)(r->freq_reg + 1);
            r->freq_msb_value = arkos_rb_read_byte(r); // line 1183
            r->freq_reg = (uint8_t)(r->freq_reg + 2);
            arkos_rb_send_vol(r, 0xFF); // lines 1199-1214
            r->wrote_psg11 = true;
            r->psg11_value = arkos_rb_read_byte(r); // line 1217
            r->wrote_psg12 = true;
            r->psg12_value = arkos_rb_read_byte(r); // line 1226
            return;
        }
        // SOFTWAREONLY, lines 1020-1108
        arkos_rb_ror(r); // noise? line 1022
        if (r->c == 1)
        {
            r->wrote_psg6 = true;
            r->psg6_value = arkos_rb_read_byte(r);
            arkos_rb_open_noise(r);
        }
        arkos_rb_send_vol(r, r->a); // lines 1040-1057 (no mask needed, bit7 known 0)
        r->wrote_freq_lsb = true;
        r->freq_lsb_value = arkos_rb_read_byte(r);
        r->wrote_freq_msb = true;
        r->freq_msb_reg = (uint8_t)(r->freq_reg + 1);
        r->freq_msb_value = arkos_rb_read_byte(r);
        r->freq_reg = (uint8_t)(r->freq_reg + 2);
        return;
    }
    arkos_rb_ror(r); // line 864
    if (r->c == 1)
    {
        // HARDWAREONLY, lines 932-1004
        arkos_rb_ror(r); // retrig? line 934
        if (r->c == 1)
        {
            r->a |= 0x80;
            r->retrig = true;
        }
        arkos_rb_ror(r); // noise? line 942
        if (r->c == 1)
        {
            r->wrote_psg6 = true;
            r->psg6_value = arkos_rb_read_byte(r);
            arkos_rb_open_noise(r);
        }
        r->wrote_psg13 = true;
        r->psg13_value = (uint8_t)(r->a & 15); // lines 961-962
        r->wrote_psg11 = true;
        r->psg11_value = arkos_rb_read_byte(r); // line 964
        r->wrote_psg12 = true;
        r->psg12_value = arkos_rb_read_byte(r); // line 973
        arkos_rb_close_tone(r); // line 984
        arkos_rb_send_vol(r, 0xFF); // lines 987-1001 (hardware volume)
        r->freq_reg = (uint8_t)(r->freq_reg + 2); // lines 1002-1003
        return;
    }
    // NOSOFT_NOHARD, lines 883-929
    arkos_rb_ror(r); // noise? line 885
    if (r->c == 1)
    {
        r->wrote_psg6 = true;
        r->psg6_value = arkos_rb_read_byte(r);
        arkos_rb_open_noise(r);
    }
    arkos_rb_send_vol(r, r->a); // "volume now in b0-b3", sent as-is (907-919)
    r->freq_reg = (uint8_t)(r->freq_reg + 2);
    arkos_rb_close_tone(r);
}

// -------------------------------------------------------------------------
// NON-INITIAL STATE (akyplayer.s lines 1238-1733)
//
// Same one-big-function rationale as arkos_rb_initial() above. Also folds
// in the reference's own "loop" mechanism (a RegisterBlock frame pointing
// at a completely different block address, still non-initial, no
// re-dispatch through the top-level initial/non-initial check) as a plain
// `continue` on the outer for(;;) rather than a recursive call -- Oscar64's
// __interrupt qualifier disallows recursive functions outright, and this
// was a real, live cycle before the restructure (dispatch -> "or_loop"
// branch -> dispatch again).
// -------------------------------------------------------------------------

static void arkos_rb_noninitial_from_byte(ArkosRB *r)
{
    // PLY_AKY_RRB_NONINITIALSTATE, lines 1270-1287
    for (;;)
    {
        arkos_rb_ror(r); // line 1271
        if (r->c == 1)
        {
            // SOFTONLY_OR_SOFTANDHARD
            arkos_rb_ror(r); // line 1348
            if (r->c == 0)
            {
                // SOFTWAREANDHARDWARE, lines 1550-1684
                arkos_rb_send_vol(r, 0xFF); // lines 1551-1566
                arkos_rb_ror(r); // LSB of hw period? line 1568
                if (r->c == 1) { r->wrote_psg11 = true; r->psg11_value = arkos_rb_read_byte(r); }
                arkos_rb_ror(r); // MSB of hw period? line 1581
                if (r->c == 1) { r->wrote_psg12 = true; r->psg12_value = arkos_rb_read_byte(r); }
                arkos_rb_ror(r); // LSB of sw period? line 1594-1595
                if (r->c == 1)
                {
                    r->wrote_freq_lsb = true;
                    r->freq_lsb_value = arkos_rb_read_byte(r);
                    // freq_reg NOT increased here on purpose (line 1621 comment)
                }
                arkos_rb_ror(r); // MSB of sw period? line 1625
                if (r->c == 1)
                {
                    // line 1630 INX (freq_reg+1) then send then line 1653
                    // DEX -- the pair cancels out, netting zero change to
                    // freq_reg itself; the MSB write targets freq_reg+1
                    // without persisting it.
                    r->wrote_freq_msb = true;
                    r->freq_msb_reg = (uint8_t)(r->freq_reg + 1);
                    r->freq_msb_value = arkos_rb_read_byte(r);
                }
                r->freq_reg = (uint8_t)(r->freq_reg + 2); // lines 1657-1659,
                                                            // the only net
                                                            // change from
                                                            // this block
                arkos_rb_ror(r); // new hw envelope? line 1662
                if (r->c == 1) { r->wrote_psg13 = true; r->psg13_value = arkos_rb_read_byte(r); }
                arkos_rb_ror(r); // retrig and/or noise? line 1677
                if (r->c == 1)
                {
                    // shared HARDWAREONLY/SOFTWAREANDHARDWARE tail, lines
                    // 1686-1733. Carry entering here is always 1 in the
                    // reference, but each ROR extracts the CURRENT bit0
                    // into carry regardless of what's rotated into bit7, so
                    // the starting carry value doesn't affect which bits of
                    // the freshly-loaded byte get tested -- only bit0
                    // (retrig), bit1 (noise), bit2 (new noise value), in
                    // order.
                    r->a = arkos_rb_read_byte(r);
                    r->c = 1;
                    arkos_rb_ror(r); // bit0 -> retrig? (line 1699)
                    if (r->c == 1) r->retrig = true;
                    arkos_rb_ror(r); // bit1 -> noise? (line 1709)
                    if (r->c == 1)
                    {
                        arkos_rb_open_noise(r);
                        arkos_rb_ror(r); // bit2 -> new noise value? (line 1723)
                        if (r->c == 1) { r->wrote_psg6 = true; r->psg6_value = arkos_rb_read_byte(r); }
                    }
                }
                return;
            }
            // SOFTWAREONLY, lines 1357-1474. ACCB = r->a (post-dispatch
            // value, unmasked).
            {
                uint8_t accb = r->a;
                uint8_t b;

                arkos_rb_send_vol(r, (uint8_t)(accb & 15)); // lines 1361-1362
                if (accb & 0x10) // bit4 (line 1380-1382)
                {
                    r->wrote_freq_lsb = true;
                    r->freq_lsb_value = arkos_rb_read_byte(r);
                    // frequency register deliberately NOT incremented here (line 1406)
                }
                if (!(accb & 0x20)) // bit5 (line 1409-1411)
                {
                    r->freq_reg = (uint8_t)(r->freq_reg + 2); // lines 1413-1414
                    return;
                }
                // MSB-and-maybe-noise byte: nipppp (lines 1417-1474)
                b = arkos_rb_read_byte(r);
                r->wrote_freq_msb = true;
                r->freq_msb_reg = (uint8_t)(r->freq_reg + 1);
                r->freq_msb_value = b;
                r->freq_reg = (uint8_t)(r->freq_reg + 2);
                if (!(b & 0x80)) return;   // ROL ACCA, carry=isNoise? (line 1446)
                arkos_rb_open_noise(r);
                if (!(b & 0x40)) return;   // second ROL, carry="new noise value?" (line 1457)
                r->wrote_psg6 = true;
                r->psg6_value = arkos_rb_read_byte(r);
            }
            return;
        }
        arkos_rb_ror(r); // line 1273
        if (r->c == 1)
        {
            // HARDWAREONLY, lines 1477-1545. Uses ROL, not ROR.
            uint8_t accb, v;
            uint8_t c1, c2;

            arkos_rb_rol(r); // line 1479
            accb = r->a;
            r->wrote_psg13 = true;
            r->psg13_value = (uint8_t)(accb & 14); // lines 1482-1483
            arkos_rb_close_tone(r); // lines 1484-1487
            arkos_rb_send_vol(r, 0xFF); // lines 1488-1501
            r->freq_reg = (uint8_t)(r->freq_reg + 2); // lines 1504-1505

            // "LDA ACCB / ROL / ROL" -- two more rotates on the SAME saved
            // accb value (not chained through r->a), lines 1508-1511.
            v = accb;
            c1 = (uint8_t)((v >> 7) & 1);
            v = (uint8_t)((v << 1) | c1);
            c2 = (uint8_t)((v >> 7) & 1);
            v = (uint8_t)((v << 1) | c2);
            r->a = v;
            r->c = c2;
            if (r->c == 1) { r->wrote_psg11 = true; r->psg11_value = arkos_rb_read_byte(r); } // line 1512
            arkos_rb_rol(r); // MSB present? line 1525
            if (r->c == 1) { r->wrote_psg12 = true; r->psg12_value = arkos_rb_read_byte(r); }
            arkos_rb_rol(r); // noise or retrig? line 1539
            if (r->c == 1)
            {
                // shared HARDWAREONLY/SOFTWAREANDHARDWARE tail, lines 1686-1733
                r->a = arkos_rb_read_byte(r);
                r->c = 1;
                arkos_rb_ror(r); // bit0 -> retrig? (line 1699)
                if (r->c == 1) r->retrig = true;
                arkos_rb_ror(r); // bit1 -> noise? (line 1709)
                if (r->c == 1)
                {
                    arkos_rb_open_noise(r);
                    arkos_rb_ror(r); // bit2 -> new noise value? (line 1723)
                    if (r->c == 1) { r->wrote_psg6 = true; r->psg6_value = arkos_rb_read_byte(r); }
                }
            }
            return;
        }

        // NOSOFT_NOHARD_OR_LOOP, lines 1283-1344. ACCB = r->a at this
        // point (post 2 rors, unmasked).
        {
            uint8_t accb = r->a;
            uint8_t masked = (uint8_t)(accb & 0x03);
            uint8_t vol_bit;

            if (masked == 0x02)
            {
                // lines 1240-1266: read a new absolute pointer, jump there,
                // re-read the first byte -- restart the whole non-initial
                // dispatch from there (matches the reference's own "we
                // KNOW it is not an initial state" comment: no re-check of
                // the initial/non-initial flag).
                uint16_t newptr = arkos_peek16(r->ptr);
                r->ptr = newptr;
                r->a = arkos_rb_read_byte(r);
                continue;
            }

            arkos_rb_close_tone(r); // lines 1290-1292
            // "LDA ACCB / ROR" -- carry-in is whatever CMP #02 left:
            // carry=1 only when masked==3 (masked can't be 2 here, loop
            // already handled above).
            r->c = (masked == 3) ? 1 : 0;
            r->a = accb;
            arkos_rb_ror(r);
            vol_bit = r->a;
            if (r->c == 1)
                arkos_rb_send_vol(r, (uint8_t)(vol_bit & 15)); // line 1297
            r->freq_reg = (uint8_t)(r->freq_reg + 2); // lines 1317-1319 (always)
            if (accb & 0x20) // bit5 of accb = original bit7 (line 1322-1323)
            {
                r->wrote_psg6 = true;
                r->psg6_value = arkos_rb_read_byte(r);
                arkos_rb_open_noise(r);
            }
        }
        return;
    }
}

// Decodes one RegisterBlock frame for one channel. `ptr`/`initial` are the
// channel's own current read position and initial/non-initial flag (see
// arkos_tick() for how these are tracked); `r7`/`vol_reg`/`freq_reg` are
// shared, evolving state threaded across all 3 channels within one tick
// (matching PLY_AKY_PLAY's own r7/volumeRegister/X). Returns the new read
// position and leaves all other results in *out.
static uint16_t arkos_decode_registerblock(uint16_t ptr, bool initial,
                                            uint8_t r7, uint8_t vol_reg, uint8_t freq_reg,
                                            ArkosRB *out)
{
    out->ptr = ptr;
    out->c = initial ? 0 : 1;
    out->r7 = r7;
    out->vol_reg = vol_reg;
    out->freq_reg = freq_reg;
    out->wrote_vol = false;
    out->wrote_freq_lsb = false;
    out->wrote_freq_msb = false;
    out->wrote_psg6 = false;
    out->wrote_psg11 = false;
    out->wrote_psg12 = false;
    out->wrote_psg13 = false;
    out->retrig = false;

    out->a = arkos_rb_read_byte(out);
    if (initial)
        arkos_rb_initial(out);
    else
        arkos_rb_noninitial_from_byte(out);

    return out->ptr;
}

// -------------------------------------------------------------------------
// Pattern/Linker advance
// -------------------------------------------------------------------------

static void arkos_advance_pattern(void)
{
    uint16_t dur;
    uint8_t i;

    dur = arkos_peek16(arkos_linker_ptr);
    if (dur == 0)
    {
        // End of song: this entry's own track-pointer field pair is
        // repurposed as an absolute pointer to loop back to.
        uint16_t loop_addr = arkos_peek16((uint16_t)(arkos_linker_ptr + 2));
        arkos_linker_ptr = loop_addr;
        dur = arkos_peek16(arkos_linker_ptr);
    }
    arkos_pattern_counter = dur;
    arkos_chan[0].track_ptr = arkos_peek16((uint16_t)(arkos_linker_ptr + 2));
    arkos_chan[1].track_ptr = arkos_peek16((uint16_t)(arkos_linker_ptr + 4));
    arkos_chan[2].track_ptr = arkos_peek16((uint16_t)(arkos_linker_ptr + 6));
    arkos_linker_ptr = (uint16_t)(arkos_linker_ptr + 8);
    // All 3 channels must read a fresh Track triple this same tick (the
    // reference achieves this via a direct jump for channel 1 and a
    // pre-set wait-counter for channels 2/3 -- functionally identical to
    // just forcing all 3 through the normal "wait expired" path here).
    for (i = 0; i < 3; i++)
        arkos_chan[i].rb_wait = 0;
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

#ifdef STORAGE_FLOPPY
bool arkos_load(uint8_t file_index)
{
    int16_t r;
    arkos_loaded = false;
    r = floppy_load(file_index, (uint8_t *)ARKOS_MODULE, ARKOS_MAX_MODULE_SIZE);
    if (r < 0)
        return false;
    arkos_loaded = true;
    return true;
}
#else
// Real Atmos ROM normally occupies $C000-$FFFF on the tape/LOCI target,
// including the real 6502 hardware IRQ vector at $FFFE/$FFFF -- ROM's own
// interrupt handler there is what chains down to rasterirq.c's low-RAM
// software vector ($0245/$0246). Enabling overlay RAM (below) banks ROM
// OUT for the whole $C000-$FFFF window, silently taking $FFFE/$FFFF with
// it: the first real Timer 1 IRQ after hrirq_start() then vectors into
// whatever garbage happens to be sitting in the freshly-banked-in RAM,
// crashing almost immediately (confirmed via a real Phosphoric capture:
// PC oscillating between $FFFF and $0002 within a couple of frames).
// tools/floppy/loader.c hits this EXACT same "no ROM means no real IRQ
// vector" problem on the floppy target and already has the fix: plant a
// tiny JMP ($0245) stub and point $FFFE/$FFFF at it -- an indirect jump
// through the same low-RAM cell rasterirq.c already manages, so
// hrirq_init()/hrirq_start() work correctly afterward regardless of what
// they've put there. Mirrored here, in ordinary (non-overlay) RAM so it
// stays valid regardless of overlay-RAM banking state.
static uint8_t arkos_irq_bridge_code[3];

static void arkos_setup_irq_bridge(void)
{
    arkos_irq_bridge_code[0] = 0x6C; // 6502 JMP (indirect) opcode
    arkos_irq_bridge_code[1] = 0x45; // operand low byte  -- $0245 (oric.h's
    arkos_irq_bridge_code[2] = 0x02; // operand high byte -- IRQ_VEC_LO/HI)

    *(volatile uint8_t *)0xFFFE = (uint8_t)((uint16_t)arkos_irq_bridge_code & 0xFF);
    *(volatile uint8_t *)0xFFFF = (uint8_t)((uint16_t)arkos_irq_bridge_code >> 8);
}

bool arkos_load(const char *path)
{
    int16_t r;
    arkos_loaded = false;
    if (!loci_present())
        return false;
    // Bank in RAM at $C000-$FFFF before loading (see arkos.h's own comment
    // for why this stays enabled for the rest of the program's runtime).
    enable_overlay_ram();
    r = file_load(path, (uint8_t *)ARKOS_MODULE, ARKOS_MAX_MODULE_SIZE);
    if (r < 0)
    {
        disable_overlay_ram();
        return false;
    }
    arkos_setup_irq_bridge();
    arkos_loaded = true;
    return true;
}
#endif

void arkos_init(void)
{
    uint8_t i;

    if (!arkos_loaded)
        return;

    // Arkos ticks conventionally at 50Hz. Timer 1 free-runs at a 100Hz rate
    // out of the box on the tape/LOCI target (oric.h's TIMER1_100HZ,
    // inherited from ROM boot) -- reprogram its latch to TIMER1_50HZ
    // (writing the latch then re-triggering via a T1C-H write, per oric.h's
    // "custom timer" note) so hrirq_add()'s callback cadence matches real
    // playback speed. This affects ANY other hrirq_add() consumer sharing
    // the same timer -- see docs/rasterirq.md and docs/arkos.md. Same
    // mechanism this project's earlier PT3 player used in its own
    // pt3_init() (archived on the `pt3` branch).
    VIA.t1llo = (uint8_t)(TIMER1_50HZ & 0xFF);
    VIA.t1lhi = (uint8_t)(TIMER1_50HZ >> 8);
    VIA.t1hi  = (uint8_t)(TIMER1_50HZ >> 8);

    // Header: byte 0 = format version (unused), byte 1 = total channel
    // count, then one 4-byte skip per group of 3 channels. This player
    // only ever supports a single PSG group (3 channels, the Oric's only
    // AY chip) -- see arkos.h -- so the header is always exactly 6 bytes
    // (2-byte version/channel-count + one fixed 4-byte group-skip): no
    // loop needed to walk a variable number of PSG groups. Deliberately
    // NOT a loop even though the original design was general over N PSG
    // groups -- a real Oscar64 -O2 miscompilation was found where an
    // equivalent `while (chan_count > 0) { ... }` loop here silently never
    // executed its body despite `chan_count` reading correctly as 3
    // (confirmed via direct Phosphoric RAM-dump instrumentation), leaving
    // arkos_linker_ptr 4 bytes short and corrupting all downstream Track/
    // RegisterBlock reads. Hardcoding the single-PSG-only offset sidesteps
    // that bug entirely rather than chasing it further.
    arkos_linker_ptr = (uint16_t)(ARKOS_MODULE + 6);
    arkos_pattern_counter = 1; // forces an immediate pattern-load on tick 1

    for (i = 0; i < 3; i++)
    {
        arkos_chan[i].track_ptr = 0;
        arkos_chan[i].rb_ptr = 0;
        arkos_chan[i].rb_wait = 0;
    }

    arkos_psg6 = 0;
    arkos_psg11 = 0;
    arkos_psg12 = 0;
    arkos_psg13 = 0;

    for (i = 0; i < 14; i++)
        arkos_ay_shadow[i] = 0;
}

// Extracted out of arkos_tick() itself: a real Oscar64 -O2 miscompilation
// was found where arkos_peek16((uint16_t)(ch->track_ptr + 1)) computed the
// WRONG address when inlined directly into arkos_tick()'s own large local-
// variable set (confirmed via Phosphoric RAM-dump instrumentation: the same
// expression, called in isolation, always returns the correct value) --
// apparently a register/zero-page-slot reuse bug tied to how many locals
// are simultaneously live in one function. Giving this its own small,
// independent stack frame sidesteps it.
static bool arkos_channel_start_triple(ArkosChannel *ch)
{
    uint8_t dur = arkos_peek(ch->track_ptr);
    uint16_t new_rb = arkos_peek16((uint16_t)(ch->track_ptr + 1));
    ch->track_ptr = (uint16_t)(ch->track_ptr + 3);
    ch->rb_ptr = new_rb;
    ch->rb_wait = dur;
    return true;
}

// Same rationale as arkos_channel_start_triple() above -- keeps the
// AY-register-application logic out of arkos_tick()'s own stack frame.
static void arkos_apply_registerblock(const ArkosRB *rb, uint8_t vol_reg,
                                       uint8_t freq_reg, bool *retrig_flag)
{
    if (rb->wrote_vol)
        arkos_ay_write_if_changed(vol_reg, rb->vol_value);
    if (rb->wrote_freq_lsb)
        arkos_ay_write_if_changed(freq_reg, rb->freq_lsb_value);
    if (rb->wrote_freq_msb)
        arkos_ay_write_if_changed(rb->freq_msb_reg, rb->freq_msb_value);
    if (rb->wrote_psg6) arkos_psg6 = rb->psg6_value;
    if (rb->wrote_psg11) arkos_psg11 = rb->psg11_value;
    if (rb->wrote_psg12) arkos_psg12 = rb->psg12_value;
    if (rb->wrote_psg13) arkos_psg13 = rb->psg13_value;
    if (rb->retrig) *retrig_flag = true;
}

__interrupt void arkos_tick(void)
{
    uint8_t i;
    uint8_t r7;
    uint8_t vol_reg;
    uint8_t freq_reg;
    bool retrig_this_tick = false;
    ArkosRB rb;

    if (!arkos_loaded)
        return;

    if (--arkos_pattern_counter == 0)
        arkos_advance_pattern();

    r7 = 0xE0;             // matches PLY_AKY_PLAY's own initial mixer template (line 665)
    vol_reg = AY_REG_VOL_A;
    freq_reg = AY_REG_TONE_A_LO;

    for (i = 0; i < 3; i++)
    {
        ArkosChannel *ch = &arkos_chan[i];
        bool initial = false;

        if (ch->rb_wait == 0)
            initial = arkos_channel_start_triple(ch);

        ch->rb_ptr = arkos_decode_registerblock(ch->rb_ptr, initial, r7, vol_reg, freq_reg, &rb);
        ch->rb_wait = (uint8_t)(ch->rb_wait - 1);

        r7 = rb.r7;
        arkos_apply_registerblock(&rb, vol_reg, freq_reg, &retrig_this_tick);

        vol_reg = (uint8_t)(vol_reg + 1);
        freq_reg = (uint8_t)(freq_reg + 2);
        // Shifts r7 by 1 between channels (LSR after channel 1, ROR after
        // channel 2 in the reference -- both are equivalent here since the
        // difference only affects bits 6/7, which the AY mixer register
        // itself ignores).
        r7 = (uint8_t)(r7 >> 1);
    }

    arkos_ay_write_if_changed(AY_REG_MIXER, (uint8_t)(r7 & 0x3F));
    arkos_ay_write_if_changed(AY_REG_NOISE, (uint8_t)(arkos_psg6 & 0x1F));
    arkos_ay_write_if_changed(AY_REG_ENV_LO, arkos_psg11);
    arkos_ay_write_if_changed(AY_REG_ENV_HI, arkos_psg12);
    // Envelope-shape is NOT routed through the write-skip check the same
    // way -- re-selecting the same shape can legitimately restart the
    // envelope generator (a retrig), so a same-value write must still go
    // through (matches pt3.c's own reasoning for AY_REG_ENV_SHAPE).
    if (retrig_this_tick || arkos_ay_shadow[AY_REG_ENV_SHAPE] != arkos_psg13)
    {
        ay_write(AY_REG_ENV_SHAPE, arkos_psg13);
        arkos_ay_shadow[AY_REG_ENV_SHAPE] = arkos_psg13;
    }
}

void arkos_stop(void)
{
    ay_write(AY_REG_VOL_A, 0);
    ay_write(AY_REG_VOL_B, 0);
    ay_write(AY_REG_VOL_C, 0);
}

const uint8_t *arkos_debug_shadow(void)
{
    return arkos_ay_shadow;
}

