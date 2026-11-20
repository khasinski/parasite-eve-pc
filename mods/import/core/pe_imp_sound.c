/* Versioned store format: magic, little-endian rate and frame count, then signed PCM16. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_imp.h"

static int valid(uint32_t rate, uint32_t frames)
{
    return rate >= 8000 && rate <= 48000 && frames && frames <= rate * 10;
}

PeImpSound *pe_imp_sound_alloc(uint32_t rate, uint32_t frames)
{
    if (!valid(rate, frames)) return NULL;
    PeImpSound *s = malloc(sizeof *s + (size_t)frames * sizeof *s->pcm);
    if (s) {
        s->rate = rate;
        s->frames = frames;
    }
    return s;
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static FILE *open_sound(const char *folder, const char *mode)
{
    char path[PE_IMP_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/sound.bin", folder) >= (int)sizeof path) return NULL;
    return fopen(path, mode);
}

static int bad(char *why, size_t n)
{
    if (why && n) snprintf(why, n, "invalid or unreadable sound.bin");
    return -1;
}

int pe_imp_sound_save(const PeImpSound *s, const char *folder, char *why, size_t n)
{
    if (!s || !valid(s->rate, s->frames)) return bad(why, n);
    FILE *f = open_sound(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t header[12] = {'P', 'C', 'M', '1'};
    put32(header + 4, s->rate);
    put32(header + 8, s->frames);
    int ok = fwrite(header, sizeof header, 1, f) == 1;
    for (uint32_t i = 0; ok && i < s->frames; ++i) {
        uint16_t sample = (uint16_t)s->pcm[i];
        uint8_t bytes[] = {(uint8_t)sample, (uint8_t)(sample >> 8)};
        ok = fwrite(bytes, sizeof bytes, 1, f) == 1;
    }
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_sound_load(const char *folder, PeImpSound **out, char *why, size_t n)
{
    *out = NULL;
    FILE *f = open_sound(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t header[12];
    PeImpSound *s = NULL;
    if (fread(header, sizeof header, 1, f) == 1 && !memcmp(header, "PCM1", 4))
        s = pe_imp_sound_alloc(get32(header + 4), get32(header + 8));
    int ok = s != NULL;
    for (uint32_t i = 0; ok && i < s->frames; ++i) {
        uint8_t bytes[2];
        ok = fread(bytes, sizeof bytes, 1, f) == 1;
        if (ok) {
            int sample = bytes[0] + bytes[1] * 256;
            s->pcm[i] = (int16_t)(sample < 32768 ? sample : sample - 65536);
        }
    }
    if (ok && (fgetc(f) != EOF || ferror(f))) ok = 0;
    fclose(f);
    if (!ok) {
        free(s);
        return bad(why, n);
    }
    *out = s;
    return 0;
}
