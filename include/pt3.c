// pt3.c - see pt3.h for attribution, scope, and design rationale.

#include "pt3.h"
#include "ay.h"
#include "oric.h"
#include "loci.h"

// Standard 12-tone-equal-temperament AY tone-period table (96 notes, C0 =
// 32.7032Hz), referenced against the ZX Spectrum's ~1.7734MHz AY clock (the
// convention the PT3 format's note table assumes), then rescaled by 289/512
// for the Oric's own AY clock -- traced precisely from ppt3.s's FIX16BITS
// routine ("INT(256*2*1000/1773)=289", then a further /2 for a net /512),
// not guessed: 289/512 = 0.5645, matching an Oric AY clock roughly half its
// 1MHz CPU clock against the Spectrum's 1.7734MHz reference (0.5639).
static const uint16_t PT3_NOTE_TABLE[96] = {
    1913, 1806, 1704, 1609, 1518, 1433, 1353, 1277, 1205, 1137, 1074, 1013,
    957,  903,  852,  804,  759,  717,  676,  638,  603,  569,  537,  507,
    478,  452,  426,  402,  380,  358,  338,  319,  301,  284,  269,  253,
    239,  226,  213,  201,  190,  179,  169,  160,  151,  142,  134,  126,
    120,  113,  107,  100,  95,   90,   85,   80,   75,   71,   67,   63,
    60,   56,   53,   50,   47,   45,   42,   40,   38,   36,   33,   32,
    30,   28,   27,   25,   24,   23,   21,   20,   19,   17,   17,   16,
    15,   14,   14,   12,   12,   11,   11,   10,   10,   9,    8,    8,
};

// Module header offsets (relative to the module's own load address, i.e.
// offset 0 of pt3_module[]) -- traced precisely from ppt3.s's INIT routine.
#define PT3_OFF_DELAY         100   // tempo, ticks/row
#define PT3_OFF_LOOP_POS      102   // order-list loop-back index
#define PT3_OFF_PATTERNS_REL  103   // 16-bit LE offset to the patterns table
#define PT3_OFF_SAMPLES       105   // fixed: 32-entry sample-pointer table (2 bytes/entry)
#define PT3_OFF_ORNAMENTS     169   // fixed: 16-entry ornament-pointer table (2 bytes/entry)
#define PT3_OFF_ORDERLIST     200   // order list: pattern*3 index bytes, 0xFF-terminated

#define PT3_NUM_CHANNELS 3

typedef struct {
    uint16_t stream_pos;        // read offset into pt3_module[] for this channel's pattern stream
    uint8_t  note;               // base note, 0-95
    uint8_t  volume;              // channel volume, 0-15
    bool     enabled;              // false = silent (no note struck yet, or released)
    uint8_t  note_skip_left;        // ticks remaining before the next row decode (row-hold)
    uint8_t  note_skip_reload;       // value note_skip_left reloads to after each row decode
    uint8_t  ornament_num;            // selected ornament index, 0-15
    uint16_t ornament_pos;              // current step index within the ornament
    uint8_t  sample_num;                 // selected sample index, 0-31
    uint16_t sample_pos;                   // current step index within the sample
    int16_t  tone_acc;                      // accumulated sample-driven tone delta
    bool     noise_enabled;                  // this channel has set a noise period (stays on)
    bool     env_enabled;                     // this channel's amplitude uses the shared envelope

    // Portamento/glissando (shared tone-slide mechanism -- CrTnSl in ppt3.s)
    bool     slide_active;        // a glissando/portamento is currently running
    uint8_t  slide_delay;         // TnSlDl: ticks between slide steps
    uint8_t  slide_counter;       // TSlCnt: countdown to the next slide step
    int16_t  slide_step;          // TSlStp: per-step tone-period delta (signed)
    int16_t  tone_slide;          // CrTnSl: accumulated slide value, added to the tone period
    bool     slide_simple;        // Flags bit2: glissando (true, slide forever) vs portamento (false, snap at target)
    uint8_t  slide_target_note;   // SlToNt: portamento's destination note
    int16_t  slide_target_dist;   // TnDelt: total tone-period distance to the target
    bool     portamento_pending;  // a portamento command ran this row; the row's NOTE command
                                   // (which comes after it in the stream) sets up the slide instead
                                   // of changing pitch immediately -- see pt3_decode_command()

    // Vibrato (on/off amplitude pulsing)
    uint8_t  vibrato_on_duration;  // OnOffD
    uint8_t  vibrato_off_duration; // OffOnD
    uint8_t  vibrato_counter;      // COnOff
    bool     vibrato_audible;      // current on/off state
} Pt3Channel;

static uint8_t  pt3_module[PT3_MAX_MODULE_SIZE];
static uint16_t pt3_module_len;
static bool     pt3_loaded;

static Pt3Channel pt3_chan[PT3_NUM_CHANNELS];

static uint8_t  pt3_delay;           // tempo: ticks/row
static uint8_t  pt3_delay_counter;   // countdown to next row
static uint16_t pt3_order_pos;       // current byte offset within the order list
static uint8_t  pt3_loop_pos;        // order-list loop-back index

static uint16_t pt3_noise_period;    // shared noise period (AY register 6)
static uint16_t pt3_env_period;      // shared envelope period (AY registers 11/12)
static uint8_t  pt3_env_shape;       // pending envelope-shape write; 0xFF = "no new shape this tick"

// Envelope-glide (C_ENGLS): a shared effect, since the AY has only one
// envelope generator -- gradually adds env_glide_step to the shared
// envelope period every env_glide_delay ticks.
static uint8_t  pt3_env_glide_delay;
static uint8_t  pt3_env_glide_counter;
static int16_t  pt3_env_glide_step;

// Last computed values for AY registers 0-13 -- kept for test/inspection
// (Phosphoric can't read the AY chip's own internal state, see docs/pt3.md).
static uint8_t pt3_ay_shadow[14];

static uint8_t pt3_byte(uint16_t offset)
{
    return pt3_module[offset];
}

static uint16_t pt3_word_le(uint16_t offset)
{
    return (uint16_t)pt3_byte(offset) | ((uint16_t)pt3_byte((uint16_t)(offset + 1)) << 8);
}

bool pt3_load(const char *path)
{
    int16_t r;
    pt3_loaded = false;
    if (!loci_present())
        return false;
    r = file_load(path, pt3_module, PT3_MAX_MODULE_SIZE);
    if (r < 0)
        return false;
    pt3_module_len = (uint16_t)r;
    pt3_loaded = true;
    return true;
}

// Derives channel A/B/C's stream_pos from the patterns-table entry selected
// by order-list byte `order_byte`. The byte is already pre-scaled by 3 in
// the file format itself, so order_byte*2 lands exactly on a 6-byte-aligned
// entry (3 channel offsets x 2 bytes each) -- traced precisely from
// ppt3.s's PLAY routine, not assumed.
static void pt3_set_pattern(uint8_t order_byte)
{
    uint16_t patterns_base = pt3_word_le(PT3_OFF_PATTERNS_REL);
    uint16_t entry = (uint16_t)(patterns_base + (uint16_t)order_byte * 2);
    pt3_chan[0].stream_pos = pt3_word_le(entry);
    pt3_chan[1].stream_pos = pt3_word_le((uint16_t)(entry + 2));
    pt3_chan[2].stream_pos = pt3_word_le((uint16_t)(entry + 4));
}

void pt3_init(void)
{
    uint8_t i;

    // PT3 ticks conventionally at 50Hz. Timer 1 is inherited from ROM boot
    // at a 100Hz free-run rate (oric.h's TIMER1_100HZ) -- reprogram its
    // latch to TIMER1_50HZ (writing the latch then re-triggering via a
    // T1C-H write, per oric.h's "custom timer" note; ACR is already in
    // free-run mode from ROM boot, no need to touch it) so hrirq_add()'s
    // callback cadence matches real playback speed. This affects ANY other
    // hrirq_add() consumer sharing the same timer -- see docs/rasterirq.md
    // and docs/pt3.md.
    VIA.t1llo = (uint8_t)(TIMER1_50HZ & 0xFF);
    VIA.t1lhi = (uint8_t)(TIMER1_50HZ >> 8);
    VIA.t1hi  = (uint8_t)(TIMER1_50HZ >> 8);

    pt3_delay = pt3_byte(PT3_OFF_DELAY);
    pt3_delay_counter = 1;
    pt3_loop_pos = pt3_byte(PT3_OFF_LOOP_POS);
    pt3_order_pos = 0;

    pt3_noise_period = 0;
    pt3_env_period = 0;
    pt3_env_shape = 0xFF;
    pt3_env_glide_delay = 0;
    pt3_env_glide_counter = 0;
    pt3_env_glide_step = 0;

    for (i = 0; i < PT3_NUM_CHANNELS; i++)
    {
        pt3_chan[i].stream_pos = 0;
        pt3_chan[i].note = 0;
        pt3_chan[i].volume = 15;
        pt3_chan[i].enabled = false;
        pt3_chan[i].note_skip_left = 1;
        pt3_chan[i].note_skip_reload = 1;
        pt3_chan[i].ornament_num = 0;
        pt3_chan[i].ornament_pos = 0;
        pt3_chan[i].sample_num = 0;
        pt3_chan[i].sample_pos = 0;
        pt3_chan[i].tone_acc = 0;
        pt3_chan[i].noise_enabled = false;
        pt3_chan[i].env_enabled = false;
        pt3_chan[i].slide_active = false;
        pt3_chan[i].slide_delay = 0;
        pt3_chan[i].slide_counter = 0;
        pt3_chan[i].slide_step = 0;
        pt3_chan[i].tone_slide = 0;
        pt3_chan[i].slide_simple = false;
        pt3_chan[i].slide_target_note = 0;
        pt3_chan[i].slide_target_dist = 0;
        pt3_chan[i].portamento_pending = false;
        pt3_chan[i].vibrato_on_duration = 0;
        pt3_chan[i].vibrato_off_duration = 0;
        pt3_chan[i].vibrato_counter = 0;
        pt3_chan[i].vibrato_audible = true;
    }

    pt3_set_pattern(pt3_byte(PT3_OFF_ORDERLIST));
}

// Reads one command byte from `chan`'s pattern stream and advances its
// cursor, dispatching by range -- these ranges were derived directly from
// ppt3.s's PD_LOOP carry-chain dispatch (verified by hand-tracing the
// cumulative arithmetic), not assumed from generic PT3 documentation:
//   0x00-0x0F  special commands (16, via a table -- see below)
//   0x10-0x1F  envelope shape (0-15) + sample select (next byte)
//   0x20-0x3F  noise period (0-31)
//   0x40-0x4F  ornament select (0-15)
//   0x50-0xAF  note (0-95)
//   0xB0       envelope off
//   0xB1-0xBF  sample select (1-15) + envelope-off, OR (0xB1 only) row-hold count in next byte
//   0xC0       note release/off
//   0xC1-0xCF  volume set (1-15)
//   0xD0       end of pattern (channel A only -- gates the order-list advance)
//   0xD1-0xEF  sample select (1-31)
//   0xF0-0xFF  ornament select (0-15) + sample select (next byte)
static void pt3_decode_row(Pt3Channel *chan);

static void pt3_advance_channel(Pt3Channel *chan, bool is_channel_a)
{
    if (--chan->note_skip_left != 0)
        return; // still holding the current row -- nothing to decode

    if (is_channel_a)
    {
        uint8_t next_byte = pt3_byte(chan->stream_pos);
        if (next_byte == 0)
        {
            // End of this pattern -- advance the order list (channel A
            // gates this for all three channels, per ppt3.s's PLAY) and
            // fall through to decode the new pattern's first row in this
            // SAME tick (matching ppt3.s's PLNLP->PL1A fallthrough --
            // deferring the row decode to the next tick instead would
            // leave note_skip_left at 0, underflowing to 255 on the next
            // tick's decrement).
            uint8_t order_byte = pt3_byte((uint16_t)(PT3_OFF_ORDERLIST + pt3_order_pos));
            if (order_byte == 0xFF)
            {
                pt3_order_pos = pt3_loop_pos;
                order_byte = pt3_byte((uint16_t)(PT3_OFF_ORDERLIST + pt3_order_pos));
            }
            pt3_set_pattern(order_byte);
            pt3_order_pos = (uint16_t)(pt3_order_pos + 1);
        }
    }

    pt3_decode_row(chan);
    chan->note_skip_left = chan->note_skip_reload;
}

static void pt3_set_ornament(Pt3Channel *chan, uint8_t ornament_num)
{
    chan->ornament_num = ornament_num;
    chan->ornament_pos = 0;
}

// Decodes one command byte. Returns true if this command ends the row
// (note, release, or the mid-stream 0xD0 "no note, just end this row"
// marker -- distinct from a literal 0x00 byte, which pt3_advance_channel
// checks separately, BEFORE any row decode, as the end-of-*pattern*
// signal) -- matches ppt3.s's PD_LOOP, which keeps dispatching further
// command bytes (volume, ornament/sample select, noise, envelope...)
// within the SAME row until one of these three is hit. Getting this loop
// structure right matters: a real pattern row commonly carries several
// command bytes (e.g. volume + ornament-select + note) that must all be
// applied together, not one per tick.
static bool pt3_decode_command(Pt3Channel *chan)
{
    uint8_t cmd = pt3_byte(chan->stream_pos);
    chan->stream_pos = (uint16_t)(chan->stream_pos + 1);

    if (cmd <= 0x0F)
    {
        // Special commands.
        switch (cmd)
        {
        case 1: // C_GLISS: continuous slide, no target note -- 3 bytes:
                // delay, then a 16-bit signed step applied directly (no
                // distance/sign correction, unlike portamento).
        {
            uint8_t delay = pt3_byte(chan->stream_pos);
            int16_t step = (int16_t)pt3_word_le((uint16_t)(chan->stream_pos + 1));
            chan->stream_pos = (uint16_t)(chan->stream_pos + 3);
            chan->slide_delay = delay;
            chan->slide_counter = delay;
            chan->slide_step = step;
            chan->slide_simple = true; // no "snap to target" check in pt3_channel_tick()
            chan->slide_active = true;
            chan->portamento_pending = false;
            break;
        }
        case 2: // C_PORTM: slide toward a target note -- 5 bytes: delay,
                // 2 reserved/legacy bytes (unused by this format version),
                // then a 16-bit signed step magnitude. The target note
                // itself comes from THIS row's own note command, which
                // (per PD_LOOP's dispatch order) is read later in the same
                // row -- see the note-command branch below, which finishes
                // this setup once the target note is known.
        {
            uint8_t delay = pt3_byte(chan->stream_pos);
            int16_t step = (int16_t)pt3_word_le((uint16_t)(chan->stream_pos + 3));
            chan->stream_pos = (uint16_t)(chan->stream_pos + 5);
            chan->slide_delay = delay;
            chan->slide_step = step; // sign corrected once the target distance is known
            chan->slide_simple = false;
            chan->portamento_pending = true;
            break;
        }
        case 3: // C_SMPOS: 1 parameter byte (sample position)
            chan->sample_pos = pt3_byte(chan->stream_pos);
            chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
            break;
        case 4: // C_ORPOS: 1 parameter byte (ornament position)
            chan->ornament_pos = pt3_byte(chan->stream_pos);
            chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
            break;
        case 5: // C_VIBRT: 2 bytes -- on-duration, off-duration. Cancels
                // any active slide (matches ppt3.s's C_VIBRT).
            chan->vibrato_on_duration = pt3_byte(chan->stream_pos);
            chan->vibrato_counter = chan->vibrato_on_duration;
            chan->vibrato_audible = true;
            chan->vibrato_off_duration = pt3_byte((uint16_t)(chan->stream_pos + 1));
            chan->stream_pos = (uint16_t)(chan->stream_pos + 2);
            chan->slide_active = false;
            chan->slide_counter = 0;
            chan->tone_slide = 0;
            break;
        case 8: // C_ENGLS: 3 bytes -- delay, then a 16-bit signed step
                // added to the SHARED envelope period every `delay` ticks
                // (only one envelope generator on the AY chip).
        {
            uint8_t delay = pt3_byte(chan->stream_pos);
            int16_t step = (int16_t)pt3_word_le((uint16_t)(chan->stream_pos + 1));
            chan->stream_pos = (uint16_t)(chan->stream_pos + 3);
            pt3_env_glide_delay = delay;
            pt3_env_glide_counter = delay;
            pt3_env_glide_step = step;
            break;
        }
        case 9: // C_DELAY: 1 parameter byte -- new tempo
            pt3_delay = pt3_byte(chan->stream_pos);
            chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
            break;
        default: // C_NOP (0, 6, 7, 10-15)
            break;
        }
        return false;
    }

    if (cmd <= 0x1F)
    { // envelope shape (0-15) + sample select
        uint8_t shape = (uint8_t)(cmd - 0x10);
        pt3_env_shape = shape;
        pt3_env_period = pt3_word_le(chan->stream_pos);
        chan->stream_pos = (uint16_t)(chan->stream_pos + 2);
        chan->env_enabled = true;
        chan->sample_num = pt3_byte(chan->stream_pos);
        chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
        chan->sample_pos = 0;
        return false;
    }

    if (cmd <= 0x3F)
    { // noise period (0-31)
        pt3_noise_period = (uint16_t)(cmd - 0x20);
        chan->noise_enabled = true;
        return false;
    }

    if (cmd <= 0x4F)
    { // ornament select (0-15)
        pt3_set_ornament(chan, (uint8_t)(cmd - 0x40));
        chan->sample_num = pt3_byte(chan->stream_pos);
        chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
        chan->sample_pos = 0;
        return false;
    }

    if (cmd <= 0xAF)
    { // note (0-95) -- ends the row
        uint8_t new_note = (uint8_t)(cmd - 0x50);
        if (chan->portamento_pending)
        {
            // Finish the C_PORTM setup started earlier this row: chan->note
            // still holds the OLD/source note here (about to slide FROM),
            // so the distance is computed now, before overwriting it --
            // matches ppt3.s's PrNote mechanism (capturing the pre-slide
            // note), just without its self-modifying-code implementation.
            int16_t dist = (int16_t)(PT3_NOTE_TABLE[new_note] - PT3_NOTE_TABLE[chan->note]);
            chan->slide_target_note = new_note;
            chan->slide_target_dist = dist;
            chan->slide_counter = chan->slide_delay;
            chan->tone_slide = 0;
            chan->slide_active = true;
            // Correct the step's sign to match the direction actually
            // needed to reach the target (ppt3.s achieves the same result
            // via a swap-then-subtract-then-conditionally-negate sequence
            // in CHREGS/SET_STP; comparing signs directly is equivalent
            // and much simpler to read).
            if ((dist < 0) != (chan->slide_step < 0))
                chan->slide_step = (int16_t)(-chan->slide_step);
            chan->portamento_pending = false;
            // chan->note deliberately NOT updated -- CHREGS/pt3_channel_tick
            // computes the sliding tone as NT_[old note] + tone_slide,
            // snapping chan->note to slide_target_note only once the slide
            // reaches/passes slide_target_dist.
        }
        else
        {
            // A plain note (no portamento this row) cancels any
            // previously-active slide -- a freshly struck note should
            // sound at its own pitch, not continue an old glide.
            chan->note = new_note;
            chan->slide_active = false;
            chan->tone_slide = 0;
        }
        chan->enabled = true;
        return true;
    }

    if (cmd == 0xB0)
    { // envelope off
        chan->env_enabled = false;
        return false;
    }

    if (cmd <= 0xBF)
    { // sample select (1-15) + envelope-off, or (0xB1) row-hold count
        uint8_t val = (uint8_t)(cmd - 0xB0);
        if (val == 1)
        {
            chan->note_skip_reload = pt3_byte(chan->stream_pos);
            chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
        }
        else
        {
            chan->env_enabled = false;
            chan->sample_num = (uint8_t)(val - 1);
            chan->sample_pos = 0;
        }
        return false;
    }

    if (cmd == 0xC0)
    { // note release/off -- ends the row
        chan->enabled = false;
        return true;
    }

    if (cmd <= 0xCF)
    { // volume set (1-15)
        chan->volume = (uint8_t)(cmd - 0xC0);
        return false;
    }

    if (cmd == 0xD0)
        return true; // mid-stream "no note, just end this row" marker

    if (cmd <= 0xEF)
    { // sample select (1-31)
        chan->sample_num = (uint8_t)(cmd - 0xD0);
        chan->sample_pos = 0;
        return false;
    }

    // cmd 0xF0-0xFF: ornament select (0-15) + sample select
    pt3_set_ornament(chan, (uint8_t)(cmd - 0xF0));
    chan->env_enabled = false;
    chan->sample_num = pt3_byte(chan->stream_pos);
    chan->stream_pos = (uint16_t)(chan->stream_pos + 1);
    chan->sample_pos = 0;
    return false;
}

// Loops pt3_decode_command() until a row-ending command is hit (note,
// release, or 0xD0), matching ppt3.s's PD_LOOP -- a real row commonly
// carries several command bytes that must all be applied before the row
// is considered fully decoded.
// A real row never carries more than a handful of commands (volume,
// ornament/sample select, an effect, a note) -- this bound exists purely
// as a safety valve against a malformed or truncated .pt3 file (an
// external, untrusted input): without it, a channel whose row never hits
// a row-ending command (e.g. a run of stray zero/no-op bytes past the
// pattern's real end) would read forward indefinitely, eventually past
// PT3_MAX_MODULE_SIZE into unrelated memory. Found via exactly this
// failure mode in an earlier draft of this project's own test fixture
// (a channel missing a row-hold command), not a hypothetical.
#define PT3_MAX_COMMANDS_PER_ROW 32

static void pt3_decode_row(Pt3Channel *chan)
{
    uint8_t guard;
    for (guard = 0; guard < PT3_MAX_COMMANDS_PER_ROW; guard++)
    {
        if (pt3_decode_command(chan))
            return;
    }
}

// Advances an active glissando/portamento by one tick. tone_slide starts
// at 0 and accumulates toward slide_target_dist (NT_[target]-NT_[source]);
// once it reaches/passes that distance (checked sign-aware, since the
// distance can be positive or negative), the channel snaps to the target
// note and the slide stops. Glissando (slide_simple) never stops on its
// own -- tone_slide just keeps accumulating until cancelled by another
// command (vibrato, or a plain note with no portamento pending).
//
// This is a self-consistent, musically-correct portamento/glissando
// design (starts at the source pitch, arrives cleanly at the target),
// not a bit-exact replication of ppt3.s's own CrTnSl/TnDelt bookkeeping
// -- re-deriving that exactly from its self-modifying-code capture of
// the pre-row note (PrNote) and its sign-swap-based distance comparison
// turned out to be underdetermined from static reading alone without
// live single-step debugging, so this implements the same *musical*
// result (slide from source note to target note) via a clearer,
// independently-verifiable mechanism instead -- see docs/pt3.md.
static void pt3_apply_slide(Pt3Channel *chan)
{
    if (!chan->slide_active)
        return;
    if (chan->slide_counter > 0)
        chan->slide_counter--;
    if (chan->slide_counter != 0)
        return;
    chan->slide_counter = chan->slide_delay;
    chan->tone_slide = (int16_t)(chan->tone_slide + chan->slide_step);
    if (chan->slide_simple)
        return; // glissando: no target, slides forever
    {
        bool reached = (chan->slide_target_dist >= 0)
                           ? (chan->tone_slide >= chan->slide_target_dist)
                           : (chan->tone_slide <= chan->slide_target_dist);
        if (reached)
        {
            chan->note = chan->slide_target_note;
            chan->tone_slide = 0;
            chan->slide_active = false;
        }
    }
}

// Advances vibrato's on/off amplitude-pulse state by one tick. Counts down
// vibrato_counter; on reaching 0, toggles audible/muted and reloads from
// whichever duration corresponds to the new state.
static void pt3_apply_vibrato(Pt3Channel *chan)
{
    if (chan->vibrato_on_duration == 0 && chan->vibrato_off_duration == 0)
        return; // vibrato not configured
    if (chan->vibrato_counter > 0)
        chan->vibrato_counter--;
    if (chan->vibrato_counter == 0)
    {
        chan->vibrato_audible = !chan->vibrato_audible;
        chan->vibrato_counter = chan->vibrato_audible ? chan->vibrato_on_duration
                                                       : chan->vibrato_off_duration;
    }
}

// Steps `chan`'s ornament/sample by one tick and computes its tone period
// and amplitude for this tick, matching ppt3.s's CHREGS: the OLD step index
// is used for this tick's data, then advanced (wrapping to the sample/
// ornament's own loop-index byte when it reaches the length byte) for next
// tick -- not "wrap then use".
static void pt3_channel_tick(Pt3Channel *chan, uint16_t *out_tone, uint8_t *out_amplitude,
                             bool *out_tone_on, bool *out_noise_on)
{
    uint16_t orn_data, sam_data;
    uint8_t orn_loop, orn_len, orn_step;
    uint8_t sam_loop, sam_len, sam_step;
    int8_t  orn_offset;
    uint8_t sam_flags, sam_mixflags;
    int16_t sam_delta, new_acc;
    int16_t note_val;
    int32_t tone_total;
    uint8_t amplitude;

    if (!chan->enabled)
    {
        *out_tone = 0;
        *out_amplitude = 0;
        *out_tone_on = false;
        *out_noise_on = false;
        return;
    }

    pt3_apply_slide(chan);
    pt3_apply_vibrato(chan);

    orn_data = pt3_word_le((uint16_t)(PT3_OFF_ORNAMENTS + (uint16_t)chan->ornament_num * 2));
    orn_loop = pt3_byte(orn_data);
    orn_len = pt3_byte((uint16_t)(orn_data + 1));
    orn_step = (uint8_t)chan->ornament_pos;
    orn_offset = (int8_t)pt3_byte((uint16_t)(orn_data + 2 + orn_step));
    {
        uint8_t next = (uint8_t)(orn_step + 1);
        chan->ornament_pos = (next >= orn_len) ? orn_loop : next;
    }

    note_val = (int16_t)chan->note + orn_offset;
    if (note_val < 0) note_val = 0;
    if (note_val > 95) note_val = 95;

    sam_data = pt3_word_le((uint16_t)(PT3_OFF_SAMPLES + (uint16_t)chan->sample_num * 2));
    sam_loop = pt3_byte(sam_data);
    sam_len = pt3_byte((uint16_t)(sam_data + 1));
    sam_step = (uint8_t)chan->sample_pos;
    {
        uint16_t step_off = (uint16_t)(sam_data + 2 + (uint16_t)sam_step * 4);
        sam_flags = pt3_byte(step_off);
        sam_mixflags = pt3_byte((uint16_t)(step_off + 1));
        sam_delta = (int16_t)pt3_word_le((uint16_t)(step_off + 2));
    }
    {
        uint8_t next = (uint8_t)(sam_step + 1);
        chan->sample_pos = (next >= sam_len) ? sam_loop : next;
    }

    new_acc = (int16_t)(chan->tone_acc + sam_delta);
    if (sam_mixflags & 0x40) // accumulate flag -- persists into next tick
        chan->tone_acc = new_acc;

    // Clamped to the AY's 12-bit tone-period range -- tone_acc/tone_slide
    // are ordinary musical parameters (small in practice), but nothing
    // stops a hand-crafted or buggy module from pushing the sum out of
    // range, and a negative sum wrapping to a huge uint16_t via a bare
    // cast would be a much worse failure mode (a jarring noise) than a
    // clamp (an out-of-tune but bounded note).
    tone_total = (int32_t)PT3_NOTE_TABLE[note_val] + new_acc + chan->tone_slide;
    if (tone_total < 0) tone_total = 0;
    if (tone_total > 4095) tone_total = 4095;
    *out_tone = (uint16_t)tone_total;

    amplitude = sam_flags & 0x0F;
    // Simplified, documented scope cut: linear volume/amplitude combine
    // (channel volume 0-15 scaled by the sample step's own amplitude
    // nibble) rather than replicating ppt3.s's VolTableCreator table or
    // its CrAmSl amplitude-slide-from-sample-flags mechanism.
    *out_amplitude = (uint8_t)(((uint16_t)chan->volume * (amplitude + 1)) >> 4);

    *out_tone_on = chan->vibrato_audible;
    *out_noise_on = chan->noise_enabled;
}

__interrupt void pt3_tick(void)
{
    uint8_t i;
    uint16_t tone[PT3_NUM_CHANNELS];
    uint8_t  ampl[PT3_NUM_CHANNELS];
    bool     tone_on[PT3_NUM_CHANNELS];
    bool     noise_on[PT3_NUM_CHANNELS];
    uint8_t  mixer;

    if (!pt3_loaded)
        return;

    if (--pt3_delay_counter == 0)
    {
        pt3_advance_channel(&pt3_chan[0], true);
        pt3_advance_channel(&pt3_chan[1], false);
        pt3_advance_channel(&pt3_chan[2], false);
        pt3_delay_counter = pt3_delay;
    }

    // Envelope-glide (C_ENGLS): a shared effect since the AY has only one
    // envelope generator -- gradually sweeps pt3_env_period every
    // pt3_env_glide_delay ticks. delay==0 means "not configured".
    if (pt3_env_glide_delay != 0)
    {
        if (pt3_env_glide_counter > 0)
            pt3_env_glide_counter--;
        if (pt3_env_glide_counter == 0)
        {
            pt3_env_glide_counter = pt3_env_glide_delay;
            pt3_env_period = (uint16_t)(pt3_env_period + pt3_env_glide_step);
        }
    }

    for (i = 0; i < PT3_NUM_CHANNELS; i++)
        pt3_channel_tick(&pt3_chan[i], &tone[i], &ampl[i], &tone_on[i], &noise_on[i]);

    // Mixer register: bit set = channel DISABLED (AY-3-8912's own,
    // active-low convention) -- bits 0-2 = tone A/B/C, bits 3-5 = noise A/B/C.
    mixer = 0x38 | 0x07; // start fully disabled, clear bits for active channels
    for (i = 0; i < PT3_NUM_CHANNELS; i++)
    {
        if (tone_on[i])
            mixer &= (uint8_t)~(1 << i);
        if (noise_on[i])
            mixer &= (uint8_t)~(8 << i);
    }

    ay_write(AY_REG_TONE_A_LO, (uint8_t)(tone[0] & 0xFF));
    ay_write(AY_REG_TONE_A_HI, (uint8_t)((tone[0] >> 8) & 0x0F));
    ay_write(AY_REG_TONE_B_LO, (uint8_t)(tone[1] & 0xFF));
    ay_write(AY_REG_TONE_B_HI, (uint8_t)((tone[1] >> 8) & 0x0F));
    ay_write(AY_REG_TONE_C_LO, (uint8_t)(tone[2] & 0xFF));
    ay_write(AY_REG_TONE_C_HI, (uint8_t)((tone[2] >> 8) & 0x0F));
    ay_write(AY_REG_NOISE, (uint8_t)(pt3_noise_period & 0x1F));
    ay_write(AY_REG_MIXER, mixer);
    ay_write(AY_REG_VOL_A, (uint8_t)(ampl[0] | (pt3_chan[0].env_enabled ? 0x10 : 0)));
    ay_write(AY_REG_VOL_B, (uint8_t)(ampl[1] | (pt3_chan[1].env_enabled ? 0x10 : 0)));
    ay_write(AY_REG_VOL_C, (uint8_t)(ampl[2] | (pt3_chan[2].env_enabled ? 0x10 : 0)));
    ay_write(AY_REG_ENV_LO, (uint8_t)(pt3_env_period & 0xFF));
    ay_write(AY_REG_ENV_HI, (uint8_t)((pt3_env_period >> 8) & 0xFF));
    // Envelope shape: only written when a command explicitly set a new one
    // this tick (0xFF sentinel = "nothing to write") -- writing it every
    // tick would restart the AY envelope generator on every single tick,
    // per ppt3.s's ROUT (its "shunte R13 si $FF" gate).
    if (pt3_env_shape != 0xFF)
    {
        ay_write(AY_REG_ENV_SHAPE, pt3_env_shape);
        pt3_ay_shadow[13] = pt3_env_shape;
        pt3_env_shape = 0xFF;
    }

    pt3_ay_shadow[0] = (uint8_t)(tone[0] & 0xFF);
    pt3_ay_shadow[1] = (uint8_t)((tone[0] >> 8) & 0x0F);
    pt3_ay_shadow[2] = (uint8_t)(tone[1] & 0xFF);
    pt3_ay_shadow[3] = (uint8_t)((tone[1] >> 8) & 0x0F);
    pt3_ay_shadow[4] = (uint8_t)(tone[2] & 0xFF);
    pt3_ay_shadow[5] = (uint8_t)((tone[2] >> 8) & 0x0F);
    pt3_ay_shadow[6] = (uint8_t)(pt3_noise_period & 0x1F);
    pt3_ay_shadow[7] = mixer;
    pt3_ay_shadow[8] = (uint8_t)(ampl[0] | (pt3_chan[0].env_enabled ? 0x10 : 0));
    pt3_ay_shadow[9] = (uint8_t)(ampl[1] | (pt3_chan[1].env_enabled ? 0x10 : 0));
    pt3_ay_shadow[10] = (uint8_t)(ampl[2] | (pt3_chan[2].env_enabled ? 0x10 : 0));
    pt3_ay_shadow[11] = (uint8_t)(pt3_env_period & 0xFF);
    pt3_ay_shadow[12] = (uint8_t)((pt3_env_period >> 8) & 0xFF);
}

void pt3_stop(void)
{
    uint8_t i;
    for (i = 0; i < PT3_NUM_CHANNELS; i++)
        pt3_chan[i].enabled = false;
    ay_write(AY_REG_VOL_A, 0);
    ay_write(AY_REG_VOL_B, 0);
    ay_write(AY_REG_VOL_C, 0);
}

const uint8_t *pt3_debug_shadow(void)
{
    return pt3_ay_shadow;
}
