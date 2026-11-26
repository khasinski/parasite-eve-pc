/* PS1 STR movies (pe_imp.h, Movie). A video sector holds a 32-byte header (u16 0x0160, u16 0x8001,
 * u16 chunk, u16 chunks in this frame, u32 frame number, u32 frame bytes, u16 width, u16 height,
 * then 12 more bytes) and 2016 bytes of the frame. A frame is its chunks in order, cut to its byte
 * count. Audio sectors interleaved with them are kept from the XA records. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_xa.h"

#define CHUNK 2016u
#define MAX_FRAMES 65536u
#define SECTOR_BYTES 2048u
#define NO_SECTOR UINT32_MAX

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }

void pe_imp_movie_free(PeImpMovie *m) {
    if (!m) return;
    free(m->offset), free(m->size), free(m->frame_data), free(m->audio);
    free(m->frame_sector), free(m->audio_sector), free(m->frame_number);
    free(m);
}

static PeImpMovie *movie_from_str(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size, int indexed) {
    if (!str || size < SECTOR_BYTES || size > PE_IMP_FILE_MAX) return NULL;
    PeImpMovie *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->sectors = (size + SECTOR_BYTES - 1) / SECTOR_BYTES;
    const uint32_t cap = m->sectors < MAX_FRAMES ? m->sectors : MAX_FRAMES;
    m->offset = malloc((size_t)cap * sizeof *m->offset);
    m->size = malloc((size_t)cap * sizeof *m->size);
    m->frame_sector = malloc((size_t)cap * sizeof *m->frame_sector);
    m->frame_number = malloc((size_t)cap * sizeof *m->frame_number);
    m->frame_data = malloc((size_t)size + 4);
    if (!m->offset || !m->size || !m->frame_sector || !m->frame_number || !m->frame_data) goto fail;
    uint32_t number = 0, chunks = 0, expected = 0, bytes = 0, sector = 0;
    for (uint32_t at = 0; at + SECTOR_BYTES <= size && m->frames < cap; at += SECTOR_BYTES) {
        const uint8_t *s = str + at;
        if (le16(s) != 0x0160 || le16(s + 2) != 0x8001) continue;
        const uint32_t chunk = le16(s + 4), count = le16(s + 6), total = le32(s + 12);
        const uint32_t w = le16(s + 16), h = le16(s + 18);
        if (!count || chunk >= count || !total || total > count * CHUNK ||
            !w || !h || w > 1024 || h > 1024) continue;
        if (!chunk) {
            number = le32(s + 8), chunks = 0, expected = count, bytes = total, sector = at / SECTOR_BYTES;
            if (!m->frames) m->width = w, m->height = h;
        }
        if (chunk != chunks || count != expected || total != bytes || number != le32(s + 8) ||
            w != m->width || h != m->height) {
            expected = 0;
            continue;
        }
        const uint32_t offset = chunk * CHUNK;
        if (offset < bytes) {
            const uint32_t remaining = bytes - offset;
            memcpy(m->frame_data + m->frame_bytes + offset, s + 32, remaining < CHUNK ? remaining : CHUNK);
        }
        if (++chunks == expected) {
            m->offset[m->frames] = m->frame_bytes;
            m->size[m->frames] = bytes;
            m->frame_number[m->frames] = number;
            m->frame_sector[m->frames++] = sector;
            m->frame_bytes += bytes;
            expected = 0;
        }
    }
    if (!m->frames) goto fail;
    if (!xa) return m;
    const uint32_t records = xa_size / PE_IMP_XA_RECORD;
    if (indexed && records != m->sectors) goto fail;
    for (uint32_t i = 0; i < records; i++)
        if (!indexed || (xa[(size_t)i * PE_IMP_XA_RECORD + 2] & 0x24) == 0x24) m->audio_records++;
    if (!m->audio_records) return m;
    m->audio = malloc((size_t)m->audio_records * PE_IMP_XA_RECORD);
    if (indexed) m->audio_sector = malloc((size_t)m->audio_records * sizeof *m->audio_sector);
    if (!m->audio || (indexed && !m->audio_sector)) goto fail;
    uint32_t out = 0;
    for (uint32_t i = 0; i < records; i++) {
        const uint8_t *record = xa + (size_t)i * PE_IMP_XA_RECORD;
        if (indexed && (record[2] & 0x24) != 0x24) continue;
        if (indexed) m->audio_sector[out] = i;
        memcpy(m->audio + (size_t)out++ * PE_IMP_XA_RECORD, record, PE_IMP_XA_RECORD);
    }
    return m;
fail:
    pe_imp_movie_free(m);
    return NULL;
}

PeImpMovie *pe_imp_movie_from_str(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size) {
    return movie_from_str(str, size, xa, xa_size, 0);
}

PeImpMovie *pe_imp_movie_from_indexed_str(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size) {
    return movie_from_str(str, size, xa, xa_size, 1);
}

int pe_imp_movie_clip(const PeImpMovie *m, uint32_t start_sector, uint32_t last_number, PeImpMovieClip *clip) {
    *clip = (PeImpMovieClip){0, m->frames, 0, m->sectors};
    if (!start_sector && !last_number) return m->frames != 0;
    if (!m->frame_number || !m->frame_sector) return 0;

    uint32_t first = 0;
    while (first < m->frames && m->frame_sector[first] < start_sector) first++;
    if (first == m->frames || (last_number && m->frame_number[first] > last_number)) return 0;

    uint32_t last = first;
    /* The original player stops after displaying the threshold frame, even
     * when the source skips that exact number. A wrap also ends the clip. */
    while (last + 1 < m->frames &&
           (!last_number || m->frame_number[last] < last_number) &&
           m->frame_number[last + 1] >= m->frame_number[last]) last++;
    *clip = (PeImpMovieClip){first, last + 1, start_sector,
                            last + 1 < m->frames ? m->frame_sector[last + 1] : m->sectors};
    return 1;
}

int16_t *pe_imp_movie_pcm(const PeImpMovie *m, const PeImpMovieClip *clip, unsigned file, unsigned channel,
                          uint32_t *frames, unsigned *rate, unsigned *channels) {
    *frames = *rate = *channels = 0;
    if (!m->audio_sector || clip->start_sector >= clip->end_sector || clip->end_sector > m->sectors) return NULL;
    int16_t *pcm = NULL;
    unsigned coding = 0;
    int32_t state[2][2] = {{0}};
    uint32_t copied_end = 0;
    for (uint32_t i = 0; i < m->audio_records; i++) {
        const uint8_t *record = m->audio + (size_t)i * PE_IMP_XA_RECORD;
        const uint32_t sector = m->audio_sector[i];
        if (sector < clip->start_sector || sector >= clip->end_sector ||
            record[0] != file || record[1] != channel) continue;
        if (!pcm) {
            coding = record[3] & 0x15;
            *rate = coding & 4 ? 18900 : 37800;
            *channels = coding & 1 ? 2 : 1;
            const uint64_t count = (uint64_t)(clip->end_sector - clip->start_sector) * (*rate / PE_IMP_MOVIE_SECTOR_HZ);
            if (!count || count * *channels * sizeof *pcm > PE_IMP_MOVIE_MAX) return NULL;
            *frames = (uint32_t)count;
            pcm = calloc((size_t)*frames * *channels, sizeof *pcm);
            if (!pcm) return NULL;
        }
        if ((record[3] & 0x15) != coding) {
            free(pcm);
            return NULL;
        }
        const uint32_t first = (sector - clip->start_sector) * (*rate / PE_IMP_MOVIE_SECTOR_HZ);
        int16_t decoded[4032];
        unsigned count = pe_imp_xa_decode(record + 4, coding, state, decoded);
        if (count > *frames - first) count = *frames - first;
        const uint32_t overlap = first < copied_end ? copied_end - first : 0;
        if (overlap < count) {
            memcpy(pcm + (size_t)(first + overlap) * *channels, decoded + overlap * *channels,
                   (count - overlap) * *channels * sizeof *pcm);
            copied_end = first + count;
        }
    }
    return pcm;
}

static FILE *open_movie(const char *folder, const char *mode) {
    char path[PE_IMP_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/movie.bin", folder) >= (int)sizeof path) return NULL;
    return fopen(path, mode);
}

static int bad(char *why, size_t n) {
    if (why && n) snprintf(why, n, "invalid or unreadable movie.bin");
    return -1;
}

static int sectors_ok(const uint32_t *positions, uint32_t count, uint32_t sectors) {
    for (uint32_t i = 0; i < count; i++)
        if (positions[i] >= sectors || (i && positions[i] <= positions[i - 1])) return 0;
    return 1;
}

static int frames_ok(const PeImpMovie *m) {
    if (!m->frames || m->frames > MAX_FRAMES || !m->width || !m->height || m->width > 1024 || m->height > 1024) return 0;
    for (uint32_t i = 0; i < m->frames; i++)
        if (m->offset[i] > m->frame_bytes || m->size[i] > m->frame_bytes - m->offset[i]) return 0;
    if (m->frame_sector && (m->sectors > PE_IMP_FILE_MAX / SECTOR_BYTES ||
        !sectors_ok(m->frame_sector, m->frames, m->sectors))) return 0;
    if (m->audio_sector && !sectors_ok(m->audio_sector, m->audio_records, m->sectors)) return 0;
    return (uint64_t)m->frame_bytes + (uint64_t)m->audio_records * PE_IMP_XA_RECORD <= PE_IMP_MOVIE_MAX;
}

int pe_imp_movie_save(const PeImpMovie *m, const char *folder, char *why, size_t n) {
    if (!frames_ok(m)) return bad(why, n);
    FILE *f = open_movie(folder, "wb");
    if (!f) return bad(why, n);
    const int timed = m->frame_sector != NULL;
    const int numbered = timed && m->frame_number;
    const unsigned version = numbered ? 3 : timed ? 2 : 1;
    uint8_t h[28] = {'M', 'O', 'V', (uint8_t)('0' + version)};
    put32(h + 4, version), put32(h + 8, m->width), put32(h + 12, m->height), put32(h + 16, m->frames);
    put32(h + 20, m->audio_records), put32(h + 24, m->sectors);
    int ok = fwrite(h, timed ? 28 : 24, 1, f) == 1;
    for (uint32_t i = 0; ok && i < m->frames; i++) {
        uint8_t e[16];
        put32(e, m->offset[i]), put32(e + 4, m->size[i]);
        if (timed) put32(e + 8, m->frame_sector[i]);
        if (numbered) put32(e + 12, m->frame_number[i]);
        ok = fwrite(e, numbered ? 16 : timed ? 12 : 8, 1, f) == 1;
    }
    if (timed) for (uint32_t i = 0; ok && i < m->audio_records; i++) {
        uint8_t e[4];
        put32(e, m->audio_sector ? m->audio_sector[i] : NO_SECTOR);
        ok = fwrite(e, sizeof e, 1, f) == 1;
    }
    ok = ok && (!m->frame_bytes || fwrite(m->frame_data, 1, m->frame_bytes, f) == m->frame_bytes);
    size_t audio = (size_t)m->audio_records * PE_IMP_XA_RECORD;
    ok = ok && (!audio || fwrite(m->audio, 1, audio, f) == audio);
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_movie_load(const char *folder, PeImpMovie **out, char *why, size_t n) {
    *out = NULL;
    FILE *f = open_movie(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t h[28];
    PeImpMovie *m = calloc(1, sizeof *m);
    if (!m || fread(h, 24, 1, f) != 1) goto fail;
    const int numbered = !memcmp(h, "MOV3", 4) && le32(h + 4) == 3;
    const int timed = numbered || (!memcmp(h, "MOV2", 4) && le32(h + 4) == 2);
    if (!timed && (memcmp(h, "MOV1", 4) || le32(h + 4) != 1)) goto fail;
    if (timed && fread(h + 24, 4, 1, f) != 1) goto fail;
    const long header = timed ? 28 : 24;
    m->width = le32(h + 8), m->height = le32(h + 12), m->frames = le32(h + 16), m->audio_records = le32(h + 20);
    m->sectors = timed ? le32(h + 24) : 0;
    const unsigned frame_index_size = numbered ? 16 : timed ? 12 : 8;
    const uint64_t index = (uint64_t)m->frames * frame_index_size + (timed ? (uint64_t)m->audio_records * 4 : 0);
    const uint64_t audio = (uint64_t)m->audio_records * PE_IMP_XA_RECORD;
    if (fseek(f, 0, SEEK_END)) goto fail;
    const long end = ftell(f);
    if (end < header || !m->frames || m->frames > MAX_FRAMES || (uint64_t)(end - header) < index + audio ||
        (uint64_t)(end - header) - index > PE_IMP_MOVIE_MAX || fseek(f, header, SEEK_SET)) goto fail;
    m->frame_bytes = (uint32_t)((uint64_t)(end - header) - index - audio);
    m->offset = malloc((size_t)m->frames * sizeof *m->offset);
    m->size = malloc((size_t)m->frames * sizeof *m->size);
    m->frame_data = malloc((size_t)m->frame_bytes + 4);
    m->audio = audio ? malloc((size_t)audio) : NULL;
    if (timed) {
        m->frame_sector = malloc((size_t)m->frames * sizeof *m->frame_sector);
        if (audio) m->audio_sector = malloc((size_t)m->audio_records * sizeof *m->audio_sector);
    }
    if (numbered) m->frame_number = malloc((size_t)m->frames * sizeof *m->frame_number);
    if (!m->offset || !m->size || !m->frame_data || (audio && !m->audio) ||
        (numbered && !m->frame_number) ||
        (timed && (!m->frame_sector || (audio && !m->audio_sector)))) goto fail;
    for (uint32_t i = 0; i < m->frames; i++) {
        uint8_t e[16];
        if (fread(e, frame_index_size, 1, f) != 1) goto fail;
        m->offset[i] = le32(e), m->size[i] = le32(e + 4);
        if (timed) m->frame_sector[i] = le32(e + 8);
        if (numbered) m->frame_number[i] = le32(e + 12);
    }
    if (m->audio_sector) {
        for (uint32_t i = 0; i < m->audio_records; i++) {
            uint8_t e[4];
            if (fread(e, sizeof e, 1, f) != 1) goto fail;
            m->audio_sector[i] = le32(e);
        }
        if (m->audio_sector[0] == NO_SECTOR) {
            for (uint32_t i = 1; i < m->audio_records; i++)
                if (m->audio_sector[i] != NO_SECTOR) goto fail;
            free(m->audio_sector);
            m->audio_sector = NULL;
        }
    }
    if ((m->frame_bytes && fread(m->frame_data, 1, m->frame_bytes, f) != m->frame_bytes) ||
        (audio && fread(m->audio, 1, (size_t)audio, f) != audio) || !frames_ok(m)) goto fail;
    fclose(f);
    *out = m;
    return 0;
fail:
    fclose(f);
    pe_imp_movie_free(m);
    return bad(why, n);
}

/* Emits an STR file as "movie/<name>" (the file's name, lower case). */
int pe_imp_emit_str(PeImpSink *sink, const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size,
                    const char *path, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[32] = "", id[48], name[48];
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++)
        stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    if (!k) return 0;
    PeImpMovie *m = pe_imp_movie_from_indexed_str(str, size, xa, xa_size);
    if (!m) return 0;                                                    /* no video in it */
    snprintf(id, sizeof id, "movie/%s", stem);
    snprintf(name, sizeof name, "Movie %.*s", (int)(strchr(base, '.') ? strchr(base, '.') - base : (long)strlen(base)), base);
    if (pe_imp_emit(sink, PE_IMP_MOVIE, id, name, m)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}
