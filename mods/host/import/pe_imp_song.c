/* Portable Song store format, with little-endian fields and a bounded payload. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_imp.h"

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static size_t payload_size(uint32_t format, uint32_t main, uint32_t sub,
                           uint32_t instruments, uint32_t samples)
{
    uint64_t total = (uint64_t)main + sub + instruments + samples;
    if (total > PE_IMP_SONG_MAX) return 0;
    if (format == PE_IMP_SONG_AKAO)                   /* header with the bank's ID, then the sequence */
        return main >= 80 + 22 && !sub && !instruments && !samples ? (size_t)total : 0;
    if (format == PE_IMP_SONG_AKAO_BANK)              /* header, instrument table, samples */
        return main == 16 && !sub && instruments == 128 * 64 && samples ? (size_t)total : 0;
    if (format == PE_IMP_SONG_PSX_XA)                 /* header, then whole sectors of sound groups */
        return main == 16 && !sub && !instruments && samples && samples % 2304 == 0 ? (size_t)total : 0;
    if (format != PE_IMP_SONG_PSX_SEQ_VAB || main < 15 || (sub && sub < 15) || instruments < 32)
        return 0;
    return (size_t)total;
}

PeImpSong *pe_imp_song_alloc(uint32_t format, uint32_t main, uint32_t sub,
                            uint32_t instruments, uint32_t samples)
{
    size_t bytes = payload_size(format, main, sub, instruments, samples);
    if (!bytes) return NULL;
    PeImpSong *song = malloc(sizeof *song + bytes);
    if (song) {
        song->format = format;
        song->seq_size[0] = main;
        song->seq_size[1] = sub;
        song->instrument_size = instruments;
        song->sample_size = samples;
    }
    return song;
}

static FILE *open_song(const char *folder, const char *mode)
{
    char path[PE_IMP_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/song.bin", folder) >= (int)sizeof path) return NULL;
    return fopen(path, mode);
}

static int bad(char *why, size_t n)
{
    if (why && n) snprintf(why, n, "invalid or unreadable song.bin");
    return -1;
}

static int has_signatures(const PeImpSong *song)
{
    if (song->format == PE_IMP_SONG_PSX_XA) return !memcmp(song->payload, "XAS1", 4);
    if (song->format == PE_IMP_SONG_AKAO) return !memcmp(song->payload, "AKS1", 4) && !memcmp(song->payload + 80, "AKAO", 4);
    if (song->format == PE_IMP_SONG_AKAO_BANK) return !memcmp(song->payload, "AKB1", 4);
    const uint8_t *sub = song->payload + song->seq_size[0];
    return !memcmp(song->payload, "pQES", 4) &&
           (!song->seq_size[1] || !memcmp(sub, "pQES", 4)) &&
           !memcmp(sub + song->seq_size[1], "pBAV", 4);
}

int pe_imp_song_save(const PeImpSong *song, const char *folder, char *why, size_t n)
{
    size_t bytes = payload_size(song->format, song->seq_size[0], song->seq_size[1],
                                song->instrument_size, song->sample_size);
    if (!bytes || !has_signatures(song)) return bad(why, n);
    FILE *f = open_song(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t header[24] = {'S', 'N', 'G', '1'};
    put32(header + 4, song->format);
    put32(header + 8, song->seq_size[0]);
    put32(header + 12, song->seq_size[1]);
    put32(header + 16, song->instrument_size);
    put32(header + 20, song->sample_size);
    int ok = fwrite(header, sizeof header, 1, f) == 1 && fwrite(song->payload, 1, bytes, f) == bytes;
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_song_load(const char *folder, PeImpSong **out, char *why, size_t n)
{
    *out = NULL;
    FILE *f = open_song(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t header[24];
    PeImpSong *song = NULL;
    if (fread(header, sizeof header, 1, f) == 1 && !memcmp(header, "SNG1", 4))
        song = pe_imp_song_alloc(get32(header + 4), get32(header + 8), get32(header + 12),
                                 get32(header + 16), get32(header + 20));
    int ok = 0;
    if (song) {
        size_t bytes = (size_t)song->seq_size[0] + song->seq_size[1] + song->instrument_size + song->sample_size;
        ok = fread(song->payload, 1, bytes, f) == bytes && fgetc(f) == EOF &&
             !ferror(f) && has_signatures(song);
    }
    fclose(f);
    if (!ok) {
        free(song);
        return bad(why, n);
    }
    *out = song;
    return 0;
}
