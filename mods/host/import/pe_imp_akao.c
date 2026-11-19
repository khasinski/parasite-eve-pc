/* First-generation AKAO playback (pe_imp_akao.h). Command table and semantics from the
 * Final Fantasy VII documentation (ff7-flat-wiki, FF7/PSX/Sound/AKAO_sequence) and Parasite Eve's
 * own driver in src/main/akao (a later generation of the same driver):
 *   0x00-0x83 note: key = byte / 11 (0-11, in the channel's octave), length = table[byte % 11];
 *   0x84-0x8E tie (the note sounds on), 0x8F-0x99 rest. A note is keyed off 2 ticks before its
 *   end unless a tie follows or legato / full-length is on. 48 ticks to a quarter note.
 *   pitch: the instrument's semitone pitch, shifted by (octave - 6); transpose and fine tune on top
 *   tempo: 0xE8 u16, quarter notes per minute = value / 214.998
 *   loops: 0xC8 marks, 0xC9 n returns up to n times, 0xCA returns forever; 0xEE jumps (from the end
 *   of its operand, as PE's driver does), 0xF0 / 0xF1 jump on the nth pass
 *   drums: 0xEC int16 (from the end of its operand) to 5-byte entries per key: instrument, note,
 *   u16 volume, pan
 * Not modelled: reverb, noise, FM, LFOs, portamento, overlay and alternate voices, slides (the
 * slide's target is taken at once). The envelope is the SPU's ADSR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_akao.h"
#include "pe_imp_spu.h"

#define CHANNELS 24
#define LOOP_DEPTH 4
#define MAX_COMMANDS 4096                          /* per channel per tick: a guard */

static const uint8_t k_args[0x60] = {             /* operand bytes of 0xA0-0xFF */
    0, 1, 1, 1, 2, 1, 0, 0, 1, 2, 1, 2, 1, 1, 1, 1,   /* A0 */
    2, 1, 1, 0, 3, 1, 0, 1, 3, 1, 0, 1, 2, 1, 0, 1,   /* B0 */
    1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1,   /* C0 */
    0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 2, 2, 2,   /* D0 */
    0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 2, 3, 2, 0, 2, 3,   /* E0 */
    3, 3, 1, 0, 2, 0, 1, 2, 1, 0, 0, 0, 0, 2, 2, 0,   /* F0 */
};
static const uint16_t k_lengths[11] = {192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4};

typedef struct { int16_t *pcm; uint32_t frames, loop; int loops, decoded; } Wave;

typedef struct {
    int on, used, ended;
    uint32_t pc, start;
    int32_t wait;                                  /* ticks to the next command */
    int32_t gate;                                  /* ticks until key-off, -1 none */
    int octave, transpose, fine, instrument, volume, master, pan;
    int legato, full, next_length, drum;
    uint32_t drum_map;
    uint32_t loop_pc[LOOP_DEPTH];
    int loop_count[LOOP_DEPTH], depth;
    int ar, dr, sl, sr, rr, am, sm, rm;            /* envelope (SPU rates and modes) */
    /* the voice */
    const Wave *w;
    uint64_t pos;
    uint32_t step;
    int phase, key_vol, key_pan;
    int32_t level;
    uint32_t counter;
} Channel;

struct PeImpAkaoPlayer {
    uint8_t *seq, *table, *samples;
    uint32_t seq_size, sample_size, sample_base;
    Wave wave[128];
    Channel ch[CHANNELS];
    double frames_per_tick, until_tick;
    int loops_left, forever, jumps, ended, lead;   /* lead: the channel whose loops count */
    unsigned errors;
};

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

static void set_tempo(PeImpAkaoPlayer *p, unsigned tempo) {
    if (!tempo) return;
    double bpm = tempo / 214.998204, ticks = bpm * 48.0 / 60.0;
    p->frames_per_tick = PE_IMP_AKAO_RATE / ticks;
}

/* an instrument's sample, decoded once: from its address to the end block; loops at its loop
 * address when the end block repeats */
static const Wave *wave(PeImpAkaoPlayer *p, int ins) {
    Wave *w = &p->wave[ins & 127];
    if (w->decoded) return w->frames ? w : NULL;
    w->decoded = 1;
    const uint8_t *r = p->table + 64 * (ins & 127);
    uint32_t addr = le32(r), loop = le32(r + 4);
    if (addr < p->sample_base || addr - p->sample_base >= p->sample_size) return NULL;
    uint32_t start = addr - p->sample_base, end = start;
    int repeat = 0;
    while (end + 16 <= p->sample_size) {
        unsigned fl = p->samples[end + 1];
        end += 16;
        if (fl & 1) { repeat = (fl & 2) != 0; break; }
        if (end - start > (4u << 20)) break;
    }
    if (end <= start || (end - start) % 16) return NULL;
    w->pcm = malloc((end - start) / 16 * 28 * sizeof *w->pcm);
    if (!w->pcm || pe_imp_spu_decode(p->samples + start, end - start, w->pcm)) { free(w->pcm), w->pcm = NULL; return NULL; }
    w->frames = (end - start) / 16 * 28;
    if (repeat && loop >= addr && loop - addr < end - start && (loop - addr) % 16 == 0)
        w->loop = (loop - addr) / 16 * 28, w->loops = 1;
    return w;
}

static void load_envelope(PeImpAkaoPlayer *p, Channel *c, int ins) {
    const uint8_t *r = p->table + 64 * (ins & 127);
    c->ar = r[8] & 0x7f, c->dr = r[9] & 15, c->sl = r[10] & 15, c->sr = r[11] & 0x7f, c->rr = r[12] & 31;
    c->am = r[13] == 5, c->sm = r[14] ? r[14] : 3, c->rm = r[15] == 7;
}

static void load_instrument(PeImpAkaoPlayer *p, Channel *c, int ins) {
    c->instrument = ins & 127;
    load_envelope(p, c, ins);
}

/* the SPU pitch of a note on an instrument (PE's Akao_LookupPitchPeriod) */
static uint32_t pitch_of(PeImpAkaoPlayer *p, int ins, int note, int fine) {
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    int octave = note / 12;
    uint32_t period = le32(p->table + 64 * (ins & 127) + 16 + 4 * (uint32_t)(note % 12));
    if (fine) period = (uint32_t)((int64_t)period + (int64_t)period * fine / 128);
    if (octave >= 7) period <<= (octave - 6);
    else if (octave < 6) period >>= (6 - octave);
    return period > 0x3fff ? 0x3fff : period;
}

static void key_on(PeImpAkaoPlayer *p, Channel *c, int ins, int note, int vol, int pan) {
    const Wave *w = wave(p, ins);
    if (!w) { c->on = 0; return; }
    c->w = w, c->pos = 0, c->phase = 0, c->level = 0, c->counter = 0, c->on = 1;
    c->step = pitch_of(p, ins, note, c->fine) << 4;    /* SPU pitch 0x1000 = 44.1 kHz = 1.0 in 16.16 */
    c->key_vol = vol, c->key_pan = pan;
}

/* ---- the envelope: the SPU's ADSR ---- */
static void env_step(Channel *v, int rate, int exponential, int up) {
    int shift = rate >> 2, step = up ? 7 - (rate & 3) : -8 + (rate & 3);
    uint32_t cycles = 1u << (shift > 11 ? shift - 11 : 0);
    int32_t s = step * (1 << (shift < 11 ? 11 - shift : 0));
    if (exponential && up && v->level > 0x6000) cycles *= 4;
    if (exponential && !up) s = (int32_t)((int64_t)s * v->level >> 15);
    if (++v->counter < cycles) return;
    v->counter = 0;
    v->level += s;
    if (v->level > 0x7fff) v->level = 0x7fff;
    if (v->level < 0) v->level = 0;
}

static void envelope(Channel *v) {
    switch (v->phase) {
    case 0:
        env_step(v, v->ar, v->am, 1);
        if (v->level >= 0x7fff) v->phase = 1, v->counter = 0;
        break;
    case 1: {
        int32_t sustain = (v->sl + 1) << 11;
        env_step(v, v->dr << 2, 1, 0);
        if (v->level <= sustain) v->level = sustain > 0x7fff ? 0x7fff : sustain, v->phase = 2, v->counter = 0;
        break;
    }
    case 2: env_step(v, v->sr, (v->sm & 4) != 0, v->sm == 1 || v->sm == 5); break;
    default:
        env_step(v, v->rr << 2, v->rm, 0);
        if (!v->level) v->on = 0;
        break;
    }
}

/* ---- commands ---- */
static int byte_at(PeImpAkaoPlayer *p, Channel *c, unsigned *v) {
    if (c->pc >= p->seq_size) return -1;
    *v = p->seq[c->pc++];
    return 0;
}

static int jump(PeImpAkaoPlayer *p, Channel *c, uint32_t from, int16_t rel) {
    int64_t to = (int64_t)from + rel;
    if (to < 20 || to >= p->seq_size) return -1;
    if (rel < 0 && c->depth == 0) {               /* the song's endless loop */
        if (!p->forever && c == &p->ch[p->lead] && ++p->jumps >= p->loops_left) p->ended = 1;
        if (p->ended) return -1;                  /* played as many times as asked: stop here */
    }
    c->pc = (uint32_t)to;
    return 0;
}

/* run a channel's commands up to its next note, tie or rest; -1 when it stops */
static int step_channel(PeImpAkaoPlayer *p, Channel *c) {
    for (int guard = 0; guard < MAX_COMMANDS; guard++) {
        unsigned op, a = 0, b = 0, d = 0;
        if (byte_at(p, c, &op)) return -1;
        if (op < 0x9a) {
            int len = c->next_length ? c->next_length : k_lengths[op % 11];
            c->next_length = 0;
            c->wait = len;
            if (op >= 0x8f) {                     /* rest */
                if (c->on && c->phase < 3) c->phase = 3, c->counter = 0;
                c->gate = -1;
                return 0;
            }
            if (op >= 0x84) return 0;             /* tie: the note sounds on */
            int key = (int)(op / 11);
            if (c->drum) {
                uint32_t e = c->drum_map + 5u * (uint32_t)key;
                if (e + 5 > p->seq_size) { p->errors++; return 0; }
                const uint8_t *m = p->seq + e;
                load_envelope(p, c, m[0]);                /* the drum's sound, the channel's instrument kept */
                key_on(p, c, m[0], m[1], (int)(le16(m + 2) >> 8) & 127, m[4] & 127);
            } else {
                key_on(p, c, c->instrument, c->octave * 12 + key + c->transpose, c->volume, c->pan);
            }
            /* key off 2 ticks before the end, unless legato or full length or a tie follows */
            unsigned next = c->pc < p->seq_size ? p->seq[c->pc] : 0;
            c->gate = (c->legato || c->full || (next >= 0x84 && next < 0x8f)) ? -1 : (len > 2 ? len - 2 : 1);
            return 0;
        }
        if (op < 0xa0) { p->errors++; continue; }
        unsigned na = k_args[op - 0xa0];
        if (c->pc + na > p->seq_size) return -1;
        uint32_t args = c->pc;
        if (na > 0) a = p->seq[args];
        if (na > 1) b = p->seq[args + 1];
        if (na > 2) d = p->seq[args + 2];
        c->pc += na;
        switch (op) {
        case 0xa0: return -1;
        case 0xa1: case 0xf2: load_instrument(p, c, (int)a); break;
        case 0xa2: c->next_length = (int)a; break;
        case 0xa3: c->master = (int)(a & 127); break;
        case 0xa5: c->octave = (int)(a & 15); break;
        case 0xa6: c->octave++; break;
        case 0xa7: c->octave--; break;
        case 0xa8: c->volume = (int)(a & 127); break;
        case 0xa9: c->volume = (int)(b & 127); break;
        case 0xaa: c->pan = (int)(a & 127); break;
        case 0xab: c->pan = (int)(b & 127); break;
        case 0xad: c->ar = (int)(a & 0x7f); break;
        case 0xae: c->dr = (int)(a & 15); break;
        case 0xaf: c->sl = (int)(a & 15); break;
        case 0xb0: c->dr = (int)(a & 15), c->sl = (int)(b & 15); break;
        case 0xb1: c->sr = (int)(a & 0x7f); break;
        case 0xb2: c->rr = (int)(a & 31); break;
        case 0xb3: load_instrument(p, c, c->instrument); break;
        case 0xb7: c->am = a == 5; break;
        case 0xbb: c->sm = (int)a; break;
        case 0xbf: c->rm = a == 7; break;
        case 0xc0: c->transpose = (int8_t)a; break;
        case 0xc1: c->transpose += (int8_t)a; break;
        case 0xc8:
            if (c->depth < LOOP_DEPTH) c->loop_pc[c->depth] = c->pc, c->loop_count[c->depth] = 0, c->depth++;
            break;
        case 0xc9:
            if (!c->depth) break;
            if (++c->loop_count[c->depth - 1] >= (a ? (int)a : 256)) c->depth--;
            else c->pc = c->loop_pc[c->depth - 1];
            break;
        case 0xca:
            if (!c->depth) break;
            c->loop_count[c->depth - 1]++;
            if (!p->forever && c == &p->ch[p->lead] && ++p->jumps >= p->loops_left) p->ended = 1;
            if (p->ended) return -1;
            c->pc = c->loop_pc[c->depth - 1];
            break;
        case 0xcc: c->legato = 1; break;
        case 0xcd: c->legato = 0; break;
        case 0xd0: c->full = 1; break;
        case 0xd1: c->full = 0; break;
        case 0xd8: c->fine = (int8_t)a; break;
        case 0xd9: c->fine += (int8_t)a; break;
        case 0xe8: set_tempo(p, a | b << 8); break;
        case 0xe9: set_tempo(p, b | d << 8); break;
        case 0xec: c->drum = 1, c->drum_map = (uint32_t)((int64_t)args + 2 + (int16_t)(a | b << 8)); break;
        case 0xed: c->drum = 0; break;
        case 0xee: if (jump(p, c, args + 2, (int16_t)(a | b << 8))) return -1; break;
        case 0xf0: case 0xf1: {
            if (!c->depth) break;
            int times = a ? (int)a : 256;
            if (c->loop_count[c->depth - 1] + 1 == times) {
                if (op == 0xf1) c->depth--;
                if (jump(p, c, args + 3, (int16_t)(b | d << 8))) return -1;
            }
            break;
        }
        default: break;                            /* effects not modelled; operands skipped */
        }
        if (op >= 0xe0 && op <= 0xe7) return -1;   /* unimplemented: the driver stops there */
        if ((op >= 0xfa && op <= 0xfc) || op == 0xff) return -1;
    }
    p->errors++;
    return -1;
}

PeImpAkaoPlayer *pe_imp_akao_open(const uint8_t *seq, uint32_t seq_size, const uint8_t *table, uint32_t table_size,
                                  const uint8_t *samples, uint32_t sample_size, uint32_t sample_base, unsigned loops,
                                  char *why, size_t n) {
    if (seq_size < 22 || memcmp(seq, "AKAO", 4) || table_size < 128 * 64 || !sample_size) {
        if (why && n) snprintf(why, n, "song: not an AKAO sequence and bank");
        return NULL;
    }
    PeImpAkaoPlayer *p = calloc(1, sizeof *p);
    if (!p || !(p->seq = malloc(seq_size)) || !(p->table = malloc(128 * 64)) || !(p->samples = malloc(sample_size))) {
        pe_imp_akao_close(p);
        if (why && n) snprintf(why, n, "out of memory");
        return NULL;
    }
    memcpy(p->seq, seq, seq_size), memcpy(p->table, table, 128 * 64), memcpy(p->samples, samples, sample_size);
    p->seq_size = seq_size, p->sample_size = sample_size, p->sample_base = sample_base;
    p->forever = !loops, p->loops_left = (int)loops;
    set_tempo(p, 25800);                          /* 120 bpm until the song sets its own */
    uint32_t mask = le32(seq + 16) & 0xffffff, at = 20;
    int used = 0;
    for (int k = 0; k < CHANNELS; k++) {
        if (!(mask >> k & 1)) continue;
        if (at + 2 > seq_size) break;
        uint32_t start = at + 2 + le16(seq + at);   /* from the end of its offset field */
        Channel *c = &p->ch[k];
        if (start >= seq_size) { at += 2; continue; }
        if (!used) p->lead = k;
        c->used = 1, c->pc = c->start = start, c->octave = 4, c->volume = 100, c->master = 127, c->pan = 64;
        c->gate = -1;
        load_instrument(p, c, 0);
        at += 2, used++;
    }
    if (!used) {
        pe_imp_akao_close(p);
        if (why && n) snprintf(why, n, "song: no channels");
        return NULL;
    }
    return p;
}

static void tick(PeImpAkaoPlayer *p) {
    for (int k = 0; k < CHANNELS; k++) {
        Channel *c = &p->ch[k];
        if (!c->used || c->ended) continue;
        if (c->gate > 0 && --c->gate == 0 && c->on && c->phase < 3) c->phase = 3, c->counter = 0;
        if (--c->wait > 0) continue;
        if (p->ended || step_channel(p, c)) {
            c->ended = 1;
            if (c->on && c->phase < 3) c->phase = 3, c->counter = 0;
        }
    }
}

uint32_t pe_imp_akao_render(PeImpAkaoPlayer *p, int16_t *out, uint32_t frames) {
    uint32_t done = 0;
    for (; done < frames; done++) {
        if (p->until_tick <= 0) {
            tick(p);
            p->until_tick += p->frames_per_tick;
        }
        p->until_tick -= 1.0;
        int32_t mix[2] = {0, 0};
        int sounding = 0, running = 0;
        for (int k = 0; k < CHANNELS; k++) {
            Channel *c = &p->ch[k];
            running |= c->used && !c->ended;
            if (!c->on) continue;
            sounding = 1;
            uint32_t f = (uint32_t)(c->pos >> 32);
            if (f >= c->w->frames) {
                if (!c->w->loops) { c->on = 0; continue; }
                f = c->w->loop + (f - c->w->frames) % (c->w->frames - c->w->loop);
                c->pos = (uint64_t)f << 32 | (c->pos & 0xffffffffu);
            }
            uint32_t g = f + 1 < c->w->frames ? f + 1 : c->w->loops ? c->w->loop : f;
            int32_t s0 = c->w->pcm[f], s1 = c->w->pcm[g], frac = (int32_t)((c->pos >> 16) & 0xffff);
            int32_t s = s0 + (int32_t)(((int64_t)(s1 - s0) * frac) >> 16);
            envelope(c);
            int64_t amp = (int64_t)s * c->level / 0x7fff * c->key_vol * c->master / (127 * 127);
            int pan = c->key_pan;
            int64_t l = pan <= 64 ? amp : amp * (127 - pan) / 63, r = pan >= 64 ? amp : amp * pan / 64;
            mix[0] += (int32_t)l, mix[1] += (int32_t)r;
            c->pos += (uint64_t)c->step << 16;
        }
        if (!running && !sounding) break;
        for (int k = 0; k < 2; k++) {
            int32_t m = mix[k] / 2;
            out[2 * done + (uint32_t)k] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
    return done;
}

unsigned pe_imp_akao_running(const PeImpAkaoPlayer *p) {
    unsigned n = 0;
    for (int k = 0; k < CHANNELS; k++) n += p->ch[k].used && !p->ch[k].ended;
    return n;
}

unsigned pe_imp_akao_errors(const PeImpAkaoPlayer *p) { return p->errors; }

void pe_imp_akao_close(PeImpAkaoPlayer *p) {
    if (!p) return;
    for (int i = 0; i < 128; i++) free(p->wave[i].pcm);
    free(p->seq), free(p->table), free(p->samples), free(p);
}
