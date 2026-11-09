/* A software AICA for the harness: plays the channels dc_spu.c starts as the
 * chip would (16-bit PCM, OCT/FNS pitch, loop points, total level and pan),
 * with linear interpolation. host_audio.c mixes them per vblank. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../../../third_party/psyz/psyz/src/dreamcast/dc_aica.h"

#define VOICES 32
#define PCM_BYTES (512 * 1024 / 16 * 56)

typedef struct {
    int on, loop;
    uint32_t start, length, loop_start;
    unsigned pitch_reg;
    DcAicaLevel level;
    uint64_t pos; /* samples from start, 32 fractional bits */
} Channel;

static short* pcm;
static Channel channel[VOICES];

size_t DcAica_Init(unsigned voices) {
    (void)voices;
    if (!pcm)
        pcm = calloc(1, PCM_BYTES);
    return pcm ? PCM_BYTES : 0;
}

void DcAica_Upload(size_t offset, const void* data, size_t bytes) {
    if (offset + bytes <= PCM_BYTES)
        memcpy((char*)pcm + offset, data, bytes);
}

void DcAica_Start(unsigned voice, uint32_t start, uint32_t length,
                  uint32_t loop_start, int loop, unsigned pitch,
                  DcAicaLevel level) {
    Channel* c = &channel[voice];
    c->on = 1;
    c->loop = loop;
    c->start = start;
    c->length = length & 0xFFFF;
    c->loop_start = loop_start & 0xFFFF;
    c->pitch_reg = DcAica_PitchRegister(pitch);
    c->level = level;
    c->pos = 0;
}
void DcAica_SetPitch(unsigned voice, unsigned pitch) {
    channel[voice].pitch_reg = DcAica_PitchRegister(pitch);
}
void DcAica_SetLevel(unsigned voice, DcAicaLevel level) { channel[voice].level = level; }
void DcAica_Stop(unsigned voice) { channel[voice].on = 0; }

/* Adds frames of the playing channels to out (stereo int accumulators). */
void HostAica_Render(int* out, unsigned frames) {
    for (unsigned v = 0; v < VOICES; v++) {
        Channel* c = &channel[v];
        if (!c->on || !c->pitch_reg)
            continue;
        int octave = (int)((c->pitch_reg >> 11) & 15);
        if (octave >= 8)
            octave -= 16;
        double rate = ldexp(1.0 + (c->pitch_reg & 1023) / 1024.0, octave);
        uint64_t step = (uint64_t)(rate * 4294967296.0);
        double gain = pow(10.0, -c->level.tl * 0.375 / 20.0);
        double side = pow(10.0, -3.0 * (c->level.pan & 15) / 20.0);
        double gl = gain * ((c->level.pan & 0x10) ? 1.0 : side);
        double gr = gain * ((c->level.pan & 0x10) ? side : 1.0);
        if (!(c->level.pan & 15))
            gl = gr = gain;
        for (unsigned i = 0; i < frames; i++) {
            uint32_t at = (uint32_t)(c->pos >> 32);
            if (at >= c->length) {
                if (!c->loop || c->loop_start >= c->length) {
                    c->on = 0;
                    break;
                }
                c->pos -= (uint64_t)(c->length - c->loop_start) << 32;
                at = (uint32_t)(c->pos >> 32);
            }
            uint32_t next = at + 1 < c->length ? at + 1 : (c->loop ? c->loop_start : at);
            double f = (double)(uint32_t)c->pos / 4294967296.0;
            double s = pcm[c->start + at] * (1 - f) + pcm[c->start + next] * f;
            out[i * 2] += (int)(s * gl);
            out[i * 2 + 1] += (int)(s * gr);
            c->pos += step;
        }
    }
}
