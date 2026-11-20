/* PS1 STR movies (pe_imp.h, Movie). A video sector holds a 32-byte header (u16 0x0160, u16 0x8001,
 * u16 chunk, u16 chunks in this frame, u32 frame number, u32 frame bytes, u16 width, u16 height,
 * then 12 more bytes) and 2016 bytes of the frame. A frame is its chunks in order, cut to its byte
 * count. Audio sectors interleaved with them are kept from the XA records. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"

#define CHUNK 2016u
#define MAX_FRAMES 65536u

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }

void pe_imp_movie_free(PeImpMovie *m) {
    if (!m) return;
    free(m->offset), free(m->size), free(m->frame_data), free(m->audio);
    free(m);
}

PeImpMovie *pe_imp_movie_from_str(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size) {
    PeImpMovie *m = calloc(1, sizeof *m);
    uint32_t cap = 1024;
    if (!m) return NULL;
    m->offset = malloc(cap * 4), m->size = malloc(cap * 4), m->frame_data = malloc((size_t)size + 4);
    if (!m->offset || !m->size || !m->frame_data) { pe_imp_movie_free(m); return NULL; }
    uint32_t frame = UINT32_MAX, chunks = 0, expected = 0, bytes = 0, start = 0;
    for (uint32_t at = 0; at + 2048 <= size; at += 2048) {
        const uint8_t *s = str + at;
        if (le16(s) != 0x0160 || le16(s + 2) != 0x8001) continue;      /* an audio sector, or padding */
        uint32_t chunk = le16(s + 4), count = le16(s + 6), number = le32(s + 8), total = le32(s + 12);
        uint32_t w = le16(s + 16), h = le16(s + 18);
        if (!count || chunk >= count || total > count * CHUNK || !w || !h || w > 1024 || h > 1024) continue;
        if (number != frame || chunk == 0) {                           /* a new frame begins */
            if (frame != UINT32_MAX && chunks == expected && m->frames < MAX_FRAMES) {
                if (m->frames == cap) {
                    uint32_t *o = realloc(m->offset, cap * 8), *z = o ? realloc(m->size, cap * 8) : NULL;
                    if (o) m->offset = o;
                    if (!o || !z) { pe_imp_movie_free(m); return NULL; }
                    m->size = z, cap *= 2;
                }
                m->offset[m->frames] = start, m->size[m->frames] = bytes, m->frames++;
            }
            frame = number, chunks = 0, expected = count, start = m->frame_bytes, bytes = total;
            if (!m->width) m->width = w, m->height = h;
        }
        if (chunk != chunks || w != m->width || h != m->height) { chunks = UINT32_MAX; continue; }  /* out of order: drop */
        uint32_t take = chunk + 1 == count ? bytes - chunk * CHUNK : CHUNK;
        if (chunk * CHUNK > bytes) take = 0;
        if (take > CHUNK) take = CHUNK;
        memcpy(m->frame_data + m->frame_bytes, s + 32, take);
        m->frame_bytes += take, chunks++;
    }
    if (frame != UINT32_MAX && chunks == expected && m->frames < MAX_FRAMES && m->frames < cap)
        m->offset[m->frames] = start, m->size[m->frames] = bytes, m->frames++;
    if (!m->frames) { pe_imp_movie_free(m); return NULL; }
    if (xa && xa_size >= PE_IMP_XA_RECORD) {
        m->audio_records = xa_size / PE_IMP_XA_RECORD;
        m->audio = malloc((size_t)m->audio_records * PE_IMP_XA_RECORD);
        if (!m->audio) { pe_imp_movie_free(m); return NULL; }
        memcpy(m->audio, xa, (size_t)m->audio_records * PE_IMP_XA_RECORD);
    }
    return m;
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

static int frames_ok(const PeImpMovie *m) {
    if (!m->frames || m->frames > MAX_FRAMES || !m->width || !m->height || m->width > 1024 || m->height > 1024) return 0;
    for (uint32_t i = 0; i < m->frames; i++)
        if (m->offset[i] > m->frame_bytes || m->size[i] > m->frame_bytes - m->offset[i]) return 0;
    return (uint64_t)m->frame_bytes + (uint64_t)m->audio_records * PE_IMP_XA_RECORD <= PE_IMP_MOVIE_MAX;
}

int pe_imp_movie_save(const PeImpMovie *m, const char *folder, char *why, size_t n) {
    if (!frames_ok(m)) return bad(why, n);
    FILE *f = open_movie(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t h[24] = {'M', 'O', 'V', '1'};
    put32(h + 4, 1), put32(h + 8, m->width), put32(h + 12, m->height), put32(h + 16, m->frames);
    put32(h + 20, m->audio_records);
    int ok = fwrite(h, sizeof h, 1, f) == 1;
    for (uint32_t i = 0; ok && i < m->frames; i++) {
        uint8_t e[8];
        put32(e, m->offset[i]), put32(e + 4, m->size[i]);
        ok = fwrite(e, 8, 1, f) == 1;
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
    uint8_t h[24];
    PeImpMovie *m = calloc(1, sizeof *m);
    int ok = 0;
    long end = 0;
    if (m && fread(h, sizeof h, 1, f) == 1 && !memcmp(h, "MOV1", 4) && le32(h + 4) == 1 && !fseek(f, 0, SEEK_END) &&
        (end = ftell(f)) > 0 && !fseek(f, 24, SEEK_SET)) {
        m->width = le32(h + 8), m->height = le32(h + 12), m->frames = le32(h + 16), m->audio_records = le32(h + 20);
        uint64_t index = (uint64_t)m->frames * 8, audio = (uint64_t)m->audio_records * PE_IMP_XA_RECORD;
        if (m->frames && m->frames <= MAX_FRAMES && audio <= PE_IMP_MOVIE_MAX && 24 + index + audio <= (uint64_t)end) {
            m->frame_bytes = (uint32_t)((uint64_t)end - 24 - index - audio);
            m->offset = malloc(m->frames * 4), m->size = malloc(m->frames * 4);
            m->frame_data = malloc((size_t)m->frame_bytes + 4), m->audio = audio ? malloc((size_t)audio) : NULL;
            ok = m->offset && m->size && m->frame_data && (!audio || m->audio);
            for (uint32_t i = 0; ok && i < m->frames; i++) {
                uint8_t e[8];
                ok = fread(e, 8, 1, f) == 1;
                m->offset[i] = le32(e), m->size[i] = le32(e + 4);
            }
            ok = ok && (!m->frame_bytes || fread(m->frame_data, 1, m->frame_bytes, f) == m->frame_bytes);
            ok = ok && (!audio || fread(m->audio, 1, (size_t)audio, f) == audio) && frames_ok(m);
        }
    }
    fclose(f);
    if (!ok) {
        pe_imp_movie_free(m);
        return bad(why, n);
    }
    *out = m;
    return 0;
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
    PeImpMovie *m = pe_imp_movie_from_str(str, size, xa, xa_size);
    if (!m) return 0;                                                    /* no video in it */
    snprintf(id, sizeof id, "movie/%s", stem);
    snprintf(name, sizeof name, "Movie %.*s", (int)(strchr(base, '.') ? strchr(base, '.') - base : (long)strlen(base)), base);
    if (pe_imp_emit(sink, PE_IMP_MOVIE, id, name, m)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}
