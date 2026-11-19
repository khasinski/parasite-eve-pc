/* Shared PS1 SPU ADPCM and VAB decoding. Source mods select bank layouts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_imp_spu.h"

enum { VAB_HEADER = 32, PROGRAM_TABLE = 128 * 16, TONES_PER_PROGRAM = 16 * 32,
       PREVIEW_RATE = 44100 };

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

int pe_imp_spu_decode(const uint8_t *src, uint32_t bytes, int16_t *dst)
{
    static const int filter[][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    int prev = 0, prev2 = 0;
    for (uint32_t block = 0; block < bytes; block += PE_IMP_SPU_BLOCK) {
        unsigned predictor = src[block] >> 4;
        unsigned shift = src[block] & 15;
        if (predictor >= sizeof filter / sizeof filter[0] || shift > 12) return -1;
        for (unsigned j = 0; j < PE_IMP_SPU_FRAMES; ++j) {
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

int pe_imp_spu_emit(PeImpSink *sink, const char *bank, unsigned wave,
                    const uint8_t *data, uint32_t bytes, char *why, size_t n)
{
    if (!bytes || bytes % PE_IMP_SPU_BLOCK)
        return reject(why, n, "invalid SPU waveform size");
    PeImpSound *clip = pe_imp_sound_alloc(PREVIEW_RATE, bytes / PE_IMP_SPU_BLOCK * PE_IMP_SPU_FRAMES);
    if (!clip) return reject(why, n, "SPU waveform exceeds the Sound budget");
    if (pe_imp_spu_decode(data, bytes, clip->pcm)) {
        free(clip);
        return reject(why, n, "invalid SPU ADPCM block");
    }
    char id[48], name[48], label[32];
    snprintf(label, sizeof label, "%s", bank);
    for (char *p = label; *p; ++p)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    snprintf(id, sizeof id, "sound/%s/wave%02u", bank, wave);
    snprintf(name, sizeof name, "%s waveform %02u", label, wave);
    if (pe_imp_emit(sink, PE_IMP_SOUND, id, name, clip))
        return reject(why, n, "could not catalog SPU waveform");
    return 0;
}

int pe_imp_vab_read(PeImpSink *sink, const char *bank, const uint8_t *header, uint32_t header_size,
                    const uint8_t *body, uint32_t body_size, char *why, size_t n)
{
    if (header_size < VAB_HEADER || memcmp(header, "pBAV", 4))
        return reject(why, n, "missing VAB header");
    unsigned programs = le16(header + 18), waves = le16(header + 22);
    unsigned table = VAB_HEADER + PROGRAM_TABLE + programs * TONES_PER_PROGRAM;
    if (!programs || programs > 128 || !waves || waves > 254 ||
        table + (waves + 1) * 2 > header_size || le16(header + table))
        return reject(why, n, "invalid VAB size table");

    uint32_t offset = 0;
    for (unsigned i = 1; i <= waves; ++i) {
        uint32_t bytes = le16(header + table + i * 2) * 8u;
        if (bytes > body_size - offset) return reject(why, n, "VAB waveform extends outside body");
        if (pe_imp_spu_emit(sink, bank, i, body + offset, bytes, why, n)) return -1;
        offset += bytes;
    }
    return offset == body_size ? 0 : reject(why, n, "VAB waveform sizes do not match body");
}

int pe_imp_sound_bank_name(const char *path, char bank[24])
{
    const char *stem = strrchr(path, '/');
    stem = stem ? stem + 1 : path;
    const char *dot = strrchr(stem, '.');
    if (!dot || dot == stem || dot - stem >= 24) return -1;
    for (const char *p = stem; p < dot; ++p) {
        char c = *p;
        bank[p - stem] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
    }
    bank[dot - stem] = 0;
    return 0;
}

int pe_imp_vab_pair(const uint8_t *header, uint32_t header_size, const uint8_t *body, uint32_t body_size,
                    const char *path, PeImpSink *sink, char *why, size_t n)
{
    static const unsigned offsets[] = {8, 64, 128, 192};
    char bank[24];
    if (pe_imp_sound_bank_name(path, bank)) return reject(why, n, "invalid sound bank name");
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        unsigned pos = offsets[i];
        if (header_size >= pos + 4 && !memcmp(header + pos, "pBAV", 4))
            return pe_imp_vab_read(sink, bank, header + pos, header_size - pos, body, body_size, why, n);
    }
    return reject(why, n, "unrecognized VAB wrapper");
}
