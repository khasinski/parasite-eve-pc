/* RE2 PS1 ARMS01 has a 128-byte game wrapper before its VAB header. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_src_re2.h"

enum { VAB_OFFSET = 128, VAB_HEADER = 32, PROGRAM_TABLE = 128 * 16,
       TONES_PER_PROGRAM = 16 * 32, LENGTH_TABLE = 256 * 2,
       ADPCM_BYTES = 16, ADPCM_FRAMES = 28, PREVIEW_RATE = 44100 };

static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static int reject(char *why, size_t n, const char *message)
{
    if (why && n) snprintf(why, n, "%s", message);
    return -1;
}

/* Floor signed division makes the predictor rounding independent of signed-shift behavior. */
static int floor_div64(int value)
{
    return value >= 0 ? value / 64 : (value - 63) / 64;
}

static int decode(const uint8_t *src, uint32_t bytes, PeImpSound *out)
{
    static const int filter[][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    int prev = 0, prev2 = 0;
    int16_t *dst = out->pcm;
    for (uint32_t block = 0; block < bytes; block += ADPCM_BYTES) {
        unsigned predictor = src[block] >> 4;
        unsigned shift = src[block] & 15;
        if (predictor >= sizeof filter / sizeof filter[0] || shift > 12) return -1;
        for (unsigned j = 0; j < ADPCM_FRAMES; ++j) {
            unsigned packed = src[block + 2 + j / 2];
            int nibble = (packed >> (j % 2 * 4)) & 15;
            if (nibble >= 8) nibble -= 16;
            int sample = nibble * (4096 >> shift);
            sample += floor_div64(prev * filter[predictor][0] + prev2 * filter[predictor][1] + 32);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            *dst++ = (int16_t)sample;
            prev2 = prev;
            prev = sample;
        }
    }
    return 0;
}

static int read_arms(const uint8_t *edh, uint32_t edh_size, const uint8_t *vb, uint32_t vb_size,
                     PeImpSink *sink, char *why, size_t n)
{
    if (edh_size < VAB_OFFSET + VAB_HEADER || memcmp(edh + VAB_OFFSET, "pBAV", 4))
        return reject(why, n, "missing ARMS01 VAB header");
    const uint8_t *vab = edh + VAB_OFFSET;
    unsigned programs = le16(vab + 18);
    unsigned waves = le16(vab + 22);
    unsigned lengths = VAB_OFFSET + VAB_HEADER + PROGRAM_TABLE + programs * TONES_PER_PROGRAM;
    if (!programs || programs > 128 || !waves || waves > 254 ||
        lengths + LENGTH_TABLE > edh_size || le16(edh + lengths))
        return reject(why, n, "invalid ARMS01 VAB tables");

    /* Every waveform is exposed at a neutral audition rate. VAB tone playback pitch,
     * flags, and envelopes need a verified PE binding before use as a native cue. */
    uint32_t offset = 0;
    for (unsigned i = 1; i <= waves; ++i) {
        uint32_t bytes = le16(edh + lengths + i * 2) * 8u;
        if (!bytes || bytes % ADPCM_BYTES || bytes > vb_size - offset)
            return reject(why, n, "ARMS01 waveform extends outside VB");
        PeImpSound *clip = pe_imp_sound_alloc(PREVIEW_RATE, bytes / ADPCM_BYTES * ADPCM_FRAMES);
        if (!clip) return reject(why, n, "ARMS01 waveform exceeds the Sound budget");
        if (decode(vb + offset, bytes, clip)) {
            free(clip);
            return reject(why, n, "invalid ARMS01 SPU ADPCM block");
        }
        char id[40], name[48];
        snprintf(id, sizeof id, "sound/arms01/wave%02u", i);
        snprintf(name, sizeof name, "ARMS01 waveform %02u", i);
        if (pe_imp_emit(sink, PE_IMP_SOUND, id, name, clip))
            return reject(why, n, "could not catalog ARMS01 waveform");
        offset += bytes;
    }
    if (offset != vb_size) return reject(why, n, "ARMS01 waveform sizes do not match VB");
    return 0;
}

const PeImpReader g_re2_arms01_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "arms01-vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "COMMON/SOUND/ARMS01.EDH",
    .tags = "sound weapon", .companion = "COMMON/SOUND/ARMS01.VB", .read_pair = read_arms
};
