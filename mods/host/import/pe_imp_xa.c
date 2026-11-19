/* CD-XA ADPCM decoding and playback (pe_imp_xa.h). The decoder follows the documented format:
 * sample = sign-extended nibble (or byte) << 12 (<< 8), shifted right by the unit's range (ranges
 * 13-15 act as 9), plus (old * K0 + older * K1 + 32) / 64 with the filter's K0, K1, clamped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_xa.h"

static const int k0[4] = {0, 60, 115, 98}, k1[4] = {0, 0, -52, -55};

static int floor64(int32_t v) { return v >= 0 ? v / 64 : (v - 63) / 64; }

unsigned pe_imp_xa_decode(const uint8_t *groups, unsigned coding, int32_t state[2][2], int16_t *out) {
    int stereo = coding & 1, eight = (coding >> 4) & 1;
    unsigned units = eight ? 4 : 8, frames = 0;
    for (unsigned g = 0; g < 18; g++) {
        const uint8_t *grp = groups + 128 * g;
        for (unsigned u = 0; u < units; u++) {
            unsigned ch = stereo ? (u & 1) : 0, param = grp[4 + u];
            int range = (int)(param & 15), flt = (int)((param >> 4) & 3);
            if (range > 12) range = 9;
            int32_t old = state[ch][0], older = state[ch][1];
            /* where this unit's samples land: stereo pairs share frames, mono units follow on */
            unsigned base = stereo ? frames + (u / 2) * 28 : frames + u * 28;
            for (unsigned j = 0; j < 28; j++) {
                int32_t s;
                if (eight) s = (int32_t)(int8_t)grp[16 + 4 * j + u] * 256;
                else {
                    unsigned b = grp[16 + 4 * j + u / 2];
                    int nib = (int)((u & 1) ? b >> 4 : b & 15);
                    s = (nib >= 8 ? nib - 16 : nib) * 4096;
                }
                s = (s < 0 ? -((-s) >> range) - ((-s) & ((1 << range) - 1) ? 1 : 0) : s >> range);  /* floor */
                s += floor64(old * k0[flt] + older * k1[flt] + 32);
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                older = old, old = s;
                if (stereo) out[2 * (base + j) + ch] = (int16_t)s;
                else out[base + j] = (int16_t)s;
            }
            state[ch][0] = old, state[ch][1] = older;
        }
        frames += stereo ? units / 2 * 28 : units * 28;
    }
    return frames;
}

struct PeImpXaPlayer {
    uint8_t *audio;
    uint32_t sectors, sector;
    unsigned coding, rate, loops_left;
    int forever, ended;
    int32_t state[2][2];
    int16_t buf[4032];
    unsigned buf_frames, buf_pos;
    int16_t prev[2], cur[2];
    uint32_t phase, step;                         /* 16.16 source frames per output frame */
};

/* the next source frame; 0 at the end */
static int next_frame(PeImpXaPlayer *p, int16_t f[2]) {
    while (p->buf_pos >= p->buf_frames) {
        if (p->sector == p->sectors) {
            if (!p->forever && --p->loops_left == 0) return 0;
            p->sector = 0;
            memset(p->state, 0, sizeof p->state);
        }
        p->buf_frames = pe_imp_xa_decode(p->audio + (size_t)p->sector * PE_IMP_XA_GROUPS, p->coding, p->state, p->buf);
        p->buf_pos = 0, p->sector++;
    }
    if (p->coding & 1) f[0] = p->buf[2 * p->buf_pos], f[1] = p->buf[2 * p->buf_pos + 1];
    else f[0] = f[1] = p->buf[p->buf_pos];
    p->buf_pos++;
    return 1;
}

PeImpXaPlayer *pe_imp_xa_open(const uint8_t *header, uint32_t header_size, const uint8_t *audio, uint32_t audio_size,
                              unsigned loops, char *why, size_t n) {
    if (header_size < 16 || memcmp(header, "XAS1", 4) || !audio_size || audio_size % PE_IMP_XA_GROUPS) {
        if (why && n) snprintf(why, n, "song: not an XA stream");
        return NULL;
    }
    PeImpXaPlayer *p = calloc(1, sizeof *p);
    if (!p || !(p->audio = malloc(audio_size))) {
        free(p);
        if (why && n) snprintf(why, n, "out of memory");
        return NULL;
    }
    memcpy(p->audio, audio, audio_size);
    p->sectors = audio_size / PE_IMP_XA_GROUPS;
    p->coding = header[4];
    p->rate = (p->coding & 4) ? 18900 : 37800;
    p->step = (uint32_t)(((uint64_t)p->rate << 16) / PE_IMP_XA_RATE);
    p->forever = !loops, p->loops_left = loops;
    if (!next_frame(p, p->cur)) p->ended = 1;
    memcpy(p->prev, p->cur, sizeof p->cur);
    return p;
}

uint32_t pe_imp_xa_render(PeImpXaPlayer *p, int16_t *out, uint32_t frames) {
    uint32_t done = 0;
    for (; done < frames && !p->ended; done++) {
        for (int c = 0; c < 2; c++)
            out[2 * done + (uint32_t)c] = (int16_t)(p->prev[c] + (((int32_t)(p->cur[c] - p->prev[c]) * (int32_t)p->phase) >> 16));
        p->phase += p->step;
        while (p->phase >= 0x10000) {
            p->phase -= 0x10000;
            memcpy(p->prev, p->cur, sizeof p->cur);
            if (!next_frame(p, p->cur)) { p->ended = 1; break; }
        }
    }
    return done;
}

void pe_imp_xa_close(PeImpXaPlayer *p) {
    if (!p) return;
    free(p->audio), free(p);
}
