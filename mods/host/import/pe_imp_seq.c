/* PS1 SEQ + VAB playback (pe_imp_seq.h). The formats are Sony's (libsnd), as games ship them:
 *   SEQ   "pQES", u32 version, u16 ticks per quarter (big-endian), u24 tempo in microseconds per
 *         quarter (big-endian), 2 bytes time signature; then events, each after a delta time
 *         (MIDI variable length): note off/on, controller, program, pitch bend, running status.
 *         FF 51 t t t sets the tempo; FF 2F ends. Loops: controller 99 = 20 marks the start
 *         (controller 6 then gives the count, 0 or 127 forever), controller 99 = 30 the end.
 *   VAB   32-byte header ("pBAV", ..., u16 programs at 18, tones at 20, samples at 22, master
 *         volume at 24); 128 program records of 16 bytes (tone count, volume, ..., pan at 4);
 *         per program present, 16 tone records of 32 bytes: priority, mode, volume, pan, centre
 *         note, fine tune (1/128 semitone), key range min, max, ..., bend down, bend up (12, 13),
 *         ADSR1, ADSR2 (16, 18), program, sample number (22, 1-based); then 256 u16 sample sizes
 *         in 8-byte units (entry 0 unused).
 *   ADPCM 16-byte blocks, flag byte 1: bit 2 loop start, bit 0 loop end, bit 1 repeat (else the
 *         voice stops there).
 * Voices follow the SPU: a sample at its centre note plays at 44.1 kHz; the envelope steps as the
 * SPU's ADSR does (attack, decay to the sustain level, sustain, release). Program volumes of 0
 * are taken as full (RE2's banks leave them 0; its driver doesn't use them). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_seq.h"
#include "pe_imp_spu.h"

#define MAX_EVENTS_PER_RENDER 100000             /* a guard against zero-time loops */

typedef struct {
    uint8_t vol, pan, center, fine, lo, hi, bend_down, bend_up;
    uint16_t adsr1, adsr2;
    int wave;                                     /* index into the decoded samples, -1 none */
} Tone;
typedef struct { uint8_t vol, pan, tones; Tone tone[16]; } Program;
typedef struct { uint32_t start, frames, loop; int loops; } Wave;  /* frames into pcm */

typedef struct {
    int on, channel, note, tone_vol, tone_pan;
    const Tone *t;
    const Wave *w;
    uint64_t pos;                                 /* 32.32 fixed point frames */
    uint32_t step;                                /* 16.16 */
    int phase;                                    /* 0 attack, 1 decay, 2 sustain, 3 release */
    int32_t level;                                /* 0..0x7fff */
    uint32_t counter;
    double base_ratio;
    uint64_t age;
} Voice;

typedef struct { int program, vol, pan, expr; int bend; } Channel;

struct PeImpSeqPlayer {
    uint8_t *seq;
    uint32_t seq_size, at, loop_at, end;
    int running, loop_count, seq_left, loops_left, forever, ended, has_loop;
    uint64_t jump_tick;                           /* the tick of the last jump back */
    unsigned ppq;
    uint32_t tempo;
    double frames_per_tick, until_event;          /* frames left before the next event */
    uint64_t tick;
    int master;
    Program program[128];
    Wave wave[256];
    int16_t *pcm;
    Channel ch[16];
    Voice v[PE_IMP_SEQ_VOICES];
    uint64_t clock;
};

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

static int fail(char *why, size_t n, const char *m) {
    if (why && n) snprintf(why, n, "song: %s", m);
    return -1;
}

static void retime(PeImpSeqPlayer *p) {
    p->frames_per_tick = (double)PE_IMP_SEQ_RATE * p->tempo / (1e6 * p->ppq);
}

/* ---- the bank ---- */
static int load_bank(PeImpSeqPlayer *p, const uint8_t *vab, uint32_t size, const uint8_t *smp, uint32_t smp_size,
                     char *why, size_t n) {
    enum { HEADER = 32, PROGRAMS = 128 * 16, TONES = 16 * 32 };
    if (size < HEADER + PROGRAMS || memcmp(vab, "pBAV", 4)) return fail(why, n, "no instrument bank");
    unsigned present = le16(vab + 18), waves = le16(vab + 22);
    uint64_t sizes = HEADER + PROGRAMS + (uint64_t)present * TONES;
    if (present > 128 || waves > 254 || sizes + 512 > size) return fail(why, n, "instrument bank sizes");
    p->master = vab[24] ? vab[24] : 127;
    /* samples: decode each, find its loop */
    uint64_t frames = 0;
    for (unsigned i = 1; i <= waves; i++) frames += le16(vab + sizes + 2 * i) * 8u / 16u * 28u;
    if (frames > (32u << 20)) return fail(why, n, "samples too long");
    p->pcm = malloc((size_t)(frames ? frames : 1) * sizeof *p->pcm);
    if (!p->pcm) return fail(why, n, "out of memory");
    uint32_t off = 0, at = 0;
    for (unsigned i = 1; i <= waves; i++) {
        uint32_t bytes = le16(vab + sizes + 2 * i) * 8u;
        if (bytes % 16 || bytes > smp_size - off) return fail(why, n, "a sample runs past the bank");
        if (pe_imp_spu_decode(smp + off, bytes, p->pcm + at)) return fail(why, n, "a sample doesn't decode");
        Wave *w = &p->wave[i];
        uint32_t blocks = bytes / 16, end = blocks, loop = 0;
        int repeat = 0;
        for (uint32_t b = 0; b < blocks; b++) {
            unsigned fl = smp[off + 16 * b + 1];
            if (fl & 4) loop = b;
            if (fl & 1) { end = b + 1, repeat = (fl & 2) != 0; break; }
        }
        w->start = at, w->frames = end * 28, w->loop = loop * 28, w->loops = repeat && loop < end;
        off += bytes, at += blocks * 28;
    }
    /* programs: tone blocks follow in program order, one per program that has tones */
    unsigned block = 0;
    for (unsigned pr = 0; pr < 128; pr++) {
        const uint8_t *r = vab + HEADER + 16 * pr;
        Program *g = &p->program[pr];
        g->tones = r[0] > 16 ? 16 : r[0];
        g->vol = r[1] ? r[1] : 127, g->pan = r[4];
        if (!g->tones) continue;
        if (block >= present) { g->tones = 0; continue; }
        for (unsigned t = 0; t < g->tones; t++) {
            const uint8_t *a = vab + HEADER + PROGRAMS + (size_t)block * TONES + 32 * t;
            Tone *x = &g->tone[t];
            x->vol = a[2], x->pan = a[3], x->center = a[4], x->fine = a[5], x->lo = a[6], x->hi = a[7];
            x->bend_down = a[12], x->bend_up = a[13];
            x->adsr1 = (uint16_t)le16(a + 16), x->adsr2 = (uint16_t)le16(a + 18);
            unsigned wv = le16(a + 22);
            x->wave = wv >= 1 && wv <= waves && p->wave[wv].frames ? (int)wv : -1;
        }
        block++;
    }
    return 0;
}

/* ---- the envelope (the SPU's ADSR, one step per output frame at 44.1 kHz) ---- */
static void env_step(Voice *v, int rate, int exponential, int up) {
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

static void envelope(Voice *v) {
    unsigned a1 = v->t->adsr1, a2 = v->t->adsr2;
    switch (v->phase) {
    case 0:
        env_step(v, (int)(a1 >> 8 & 0x7f), (int)(a1 >> 15), 1);
        if (v->level >= 0x7fff) v->phase = 1, v->counter = 0;
        break;
    case 1: {
        int32_t sustain = (int32_t)(((a1 & 15) + 1) << 11);
        env_step(v, (int)((a1 >> 4 & 15) << 2), 1, 0);
        if (v->level <= sustain) v->level = sustain > 0x7fff ? 0x7fff : sustain, v->phase = 2, v->counter = 0;
        break;
    }
    case 2:
        env_step(v, (int)(a2 >> 6 & 0x7f), (int)(a2 >> 15), !(a2 >> 14 & 1));
        break;
    default:
        env_step(v, (int)((a2 & 31) << 2), (int)(a2 >> 5 & 1), 0);
        if (!v->level) v->on = 0;
        break;
    }
}

/* ---- voices ---- */
static void set_pitch(PeImpSeqPlayer *p, Voice *v) {
    const Channel *c = &p->ch[v->channel];
    double semis = (c->bend >= 0 ? v->t->bend_up : v->t->bend_down) * (c->bend / 8192.0);
    double r = v->base_ratio * pow(2.0, semis / 12.0);
    if (r > 4.0) r = 4.0;                         /* the SPU's highest pitch */
    v->step = (uint32_t)(r * 65536.0);
}

static void note_on(PeImpSeqPlayer *p, int ch, int note, int vel) {
    const Program *g = &p->program[p->ch[ch].program & 127];
    for (unsigned t = 0; t < g->tones; t++) {
        const Tone *x = &g->tone[t];
        if (note < x->lo || note > x->hi || x->wave < 0) continue;
        Voice *v = NULL;
        for (int i = 0; i < PE_IMP_SEQ_VOICES && !v; i++) if (!p->v[i].on) v = &p->v[i];
        if (!v) {                                 /* steal: the quietest released voice, else the oldest */
            for (int i = 0; i < PE_IMP_SEQ_VOICES; i++)
                if (p->v[i].phase == 3 && (!v || p->v[i].level < v->level)) v = &p->v[i];
            for (int i = 0; i < PE_IMP_SEQ_VOICES && !v; i++)
                if (!v || p->v[i].age < v->age) v = &p->v[i];
            if (!v) v = &p->v[0];
        }
        memset(v, 0, sizeof *v);
        v->on = 1, v->channel = ch, v->note = note, v->t = x, v->w = &p->wave[x->wave];
        v->tone_vol = x->vol * vel / 127, v->tone_pan = x->pan;
        v->base_ratio = pow(2.0, ((note - x->center) + x->fine / 128.0) / 12.0);
        v->age = ++p->clock;
        set_pitch(p, v);
    }
}

static void note_off(PeImpSeqPlayer *p, int ch, int note) {
    for (int i = 0; i < PE_IMP_SEQ_VOICES; i++)
        if (p->v[i].on && p->v[i].channel == ch && p->v[i].note == note && p->v[i].phase < 3)
            p->v[i].phase = 3, p->v[i].counter = 0;
}

/* ---- the sequence ---- */
static int byte_at(PeImpSeqPlayer *p, unsigned *out) {
    if (p->at >= p->seq_size) return -1;
    *out = p->seq[p->at++];
    return 0;
}

static int vlq(PeImpSeqPlayer *p, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned b;
        if (byte_at(p, &b)) return -1;
        v = v << 7 | (b & 0x7f);
        if (!(b & 0x80)) { *out = v; return 0; }
    }
    return -1;
}

static void controller(PeImpSeqPlayer *p, int ch, unsigned cc, unsigned val) {
    Channel *c = &p->ch[ch];
    if (cc == 7) c->vol = (int)val;
    else if (cc == 10) c->pan = (int)val;
    else if (cc == 11) c->expr = (int)val;
    else if (cc == 99 && val == 20) {             /* loop start: just after this event */
        p->loop_at = p->at, p->has_loop = 1, p->loop_count = -1, p->jump_tick = p->tick;
    } else if (cc == 6 && p->has_loop && p->loop_count == -1) {
        p->loop_count = (int)val, p->seq_left = (int)val;
    } else if (cc == 99 && val == 30 && p->has_loop) { /* loop end: back to the start, or play on */
        if (p->tick == p->jump_tick) return;      /* no time passed: it would never end */
        if (p->loop_count > 0 && p->loop_count < 127 && p->seq_left-- <= 1) return;   /* its own count */
        if (!p->forever && p->loops_left-- <= 1) return;                            /* the caller's */
        p->at = p->loop_at, p->jump_tick = p->tick;
    }
}

/* the events due now; 0, or -1 at the end */
static int events(PeImpSeqPlayer *p) {
    int guard = 0;
    while (p->until_event <= 0) {
        if (++guard > MAX_EVENTS_PER_RENDER) return -1;
        unsigned st, a, b;
        if (byte_at(p, &st)) return -1;
        if (st < 0x80) {                          /* running status */
            if (!p->running) return -1;
            p->at--, st = (unsigned)p->running;
        } else if (st < 0xf0) p->running = (int)st;
        int ch = (int)(st & 15);
        switch (st & 0xf0) {
        case 0x80: if (byte_at(p, &a) || byte_at(p, &b)) return -1; note_off(p, ch, (int)a); break;
        case 0x90:
            if (byte_at(p, &a) || byte_at(p, &b)) return -1;
            if (b) note_on(p, ch, (int)a, (int)b); else note_off(p, ch, (int)a);
            break;
        case 0xa0: if (byte_at(p, &a) || byte_at(p, &b)) return -1; break;
        case 0xb0: if (byte_at(p, &a) || byte_at(p, &b)) return -1; controller(p, ch, a, b); break;
        case 0xc0: if (byte_at(p, &a)) return -1; p->ch[ch].program = (int)(a & 127); break;
        case 0xd0: if (byte_at(p, &a)) return -1; break;
        case 0xe0:
            if (byte_at(p, &a) || byte_at(p, &b)) return -1;
            p->ch[ch].bend = (int)((b << 7 | a)) - 8192;
            for (int i = 0; i < PE_IMP_SEQ_VOICES; i++) if (p->v[i].on && p->v[i].channel == ch) set_pitch(p, &p->v[i]);
            break;
        default:                                  /* FF: meta */
            if (st != 0xff || byte_at(p, &a)) return -1;
            if (a == 0x2f) return -1;
            if (a == 0x51) {
                unsigned t0, t1, t2;
                if (byte_at(p, &t0) || byte_at(p, &t1) || byte_at(p, &t2)) return -1;
                uint32_t tempo = t0 << 16 | t1 << 8 | t2;
                if (tempo) p->tempo = tempo, retime(p);
            } else {
                uint32_t len;
                if (vlq(p, &len) || len > p->seq_size - p->at) return -1;
                p->at += len;
            }
            break;
        }
        uint32_t delta;
        if (vlq(p, &delta)) return -1;
        p->until_event += delta * p->frames_per_tick;
        p->tick += delta;
    }
    return 0;
}

PeImpSeqPlayer *pe_imp_seq_open(const uint8_t *seq, uint32_t seq_size, const uint8_t *vab, uint32_t vab_size,
                                const uint8_t *samples, uint32_t sample_size, unsigned loops, char *why, size_t n) {
    if (!seq || seq_size < 16 || memcmp(seq, "pQES", 4)) { fail(why, n, "no sequence"); return NULL; }
    PeImpSeqPlayer *p = calloc(1, sizeof *p);
    if (!p) { fail(why, n, "out of memory"); return NULL; }
    p->seq = malloc(seq_size);
    if (!p->seq || load_bank(p, vab, vab_size, samples, sample_size, why, n)) {
        if (!p->seq) fail(why, n, "out of memory");
        pe_imp_seq_close(p);
        return NULL;
    }
    memcpy(p->seq, seq, seq_size);
    p->seq_size = seq_size;
    p->ppq = (unsigned)(seq[8] << 8 | seq[9]);
    p->tempo = (uint32_t)(seq[10] << 16 | seq[11] << 8 | seq[12]);
    if (!p->ppq || !p->tempo) { fail(why, n, "sequence timing"); pe_imp_seq_close(p); return NULL; }
    retime(p);
    p->loops_left = (int)loops, p->forever = !loops;
    for (int c = 0; c < 16; c++) p->ch[c] = (Channel){0, 127, 64, 127, 0};
    p->at = 15;
    uint32_t first;
    if (vlq(p, &first)) { fail(why, n, "sequence is empty"); pe_imp_seq_close(p); return NULL; }
    p->until_event = first * p->frames_per_tick, p->tick = first;
    return p;
}

uint32_t pe_imp_seq_render(PeImpSeqPlayer *p, int16_t *out, uint32_t frames) {
    uint32_t done = 0;
    for (; done < frames; done++) {
        if (!p->ended && events(p)) {
            p->ended = 1;
            for (int i = 0; i < PE_IMP_SEQ_VOICES; i++) if (p->v[i].on && p->v[i].phase < 3) p->v[i].phase = 3;
        }
        int any = 0;
        int32_t mix[2] = {0, 0};
        for (int i = 0; i < PE_IMP_SEQ_VOICES; i++) {
            Voice *v = &p->v[i];
            if (!v->on) continue;
            any = 1;
            uint32_t f = (uint32_t)(v->pos >> 32);
            if (f >= v->w->frames) {
                if (!v->w->loops) { v->on = 0; continue; }
                f = v->w->loop + (f - v->w->frames) % (v->w->frames - v->w->loop);
                v->pos = (uint64_t)f << 32 | (v->pos & 0xffffffffu);
            }
            uint32_t g = f + 1 < v->w->frames ? f + 1 : v->w->loops ? v->w->loop : f;
            int32_t s0 = p->pcm[v->w->start + f], s1 = p->pcm[v->w->start + g];
            int32_t frac = (int32_t)((v->pos >> 16) & 0xffff);
            int32_t s = s0 + (int32_t)(((int64_t)(s1 - s0) * frac) >> 16);
            envelope(v);
            const Channel *c = &p->ch[v->channel];
            int64_t vol = (int64_t)v->tone_vol * c->vol * c->expr * p->program[c->program].vol * p->master;
            int pan = v->tone_pan + c->pan - 64;
            pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
            int64_t amp = (int64_t)s * v->level / 0x7fff * vol / ((int64_t)127 * 127 * 127 * 127 * 127);
            int64_t l = pan <= 64 ? amp : amp * (127 - pan) / 63, r = pan >= 64 ? amp : amp * pan / 64;
            mix[0] += (int32_t)l, mix[1] += (int32_t)r;
            v->pos += (uint64_t)v->step << 16;
        }
        if (p->ended && !any) break;
        for (int k = 0; k < 2; k++) {
            int32_t m = mix[k] / 2;                       /* headroom: up to 24 voices at full volume */
            out[2 * done + (uint32_t)k] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
        p->until_event -= 1.0;
    }
    return done;
}

unsigned pe_imp_seq_voices(const PeImpSeqPlayer *p) {
    unsigned n = 0;
    for (int i = 0; i < PE_IMP_SEQ_VOICES; i++) n += p->v[i].on != 0;
    return n;
}

uint64_t pe_imp_seq_tick(const PeImpSeqPlayer *p) { return p->tick; }

void pe_imp_seq_close(PeImpSeqPlayer *p) {
    if (!p) return;
    free(p->seq), free(p->pcm), free(p);
}
