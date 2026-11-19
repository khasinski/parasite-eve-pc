/* IMG1: header, frame sizes, frames. IMG2 adds a table of per-view extra sizes
 * before the frames, and the optional extra records after all frames. Little-endian. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_imp.h"

static uint32_t get32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static size_t payload_size(uint32_t format, uint32_t w, uint32_t h, uint32_t count,
                           const uint32_t *sizes, const uint32_t *extra) {
    if (format != PE_IMP_IMAGESET_MDEC || !w || !h || w % 16 || h % 16 || w > 1024 || h > 1024 ||
        !count || count > PE_IMP_IMAGESET_VIEWS)
        return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (sizes[i] < 12) return 0;
        total += sizes[i];
        if (extra) total += extra[i];
    }
    return total <= PE_IMP_IMAGESET_MAX ? (size_t)total : 0;
}

PeImpImageSet *pe_imp_imageset_alloc_extra(uint32_t format, uint32_t w, uint32_t h, uint32_t count,
                                           const uint32_t *sizes, const uint32_t *extra) {
    size_t bytes = payload_size(format, w, h, count, sizes, extra);
    if (!bytes) return NULL;
    PeImpImageSet *set = calloc(1, sizeof *set + bytes);
    if (set) {
        set->format = format;
        set->width = w;
        set->height = h;
        set->count = count;
        memcpy(set->size, sizes, count * sizeof *sizes);
        if (extra) memcpy(set->extra, extra, count * sizeof *extra);
    }
    return set;
}

PeImpImageSet *pe_imp_imageset_alloc(uint32_t format, uint32_t w, uint32_t h, uint32_t count, const uint32_t *sizes) {
    return pe_imp_imageset_alloc_extra(format, w, h, count, sizes, NULL);
}

const uint8_t *pe_imp_imageset_frame(const PeImpImageSet *set, uint32_t i, uint32_t *size) {
    if (i >= set->count) return NULL;
    size_t at = 0;
    for (uint32_t k = 0; k < i; k++) at += set->size[k];
    if (size) *size = set->size[i];
    return set->payload + at;
}

const uint8_t *pe_imp_imageset_extra(const PeImpImageSet *set, uint32_t i, uint32_t *size) {
    if (size) *size = 0;
    if (i >= set->count || !set->extra[i]) return NULL;
    size_t at = 0;
    for (uint32_t k = 0; k < set->count; k++) at += set->size[k];
    for (uint32_t k = 0; k < i; k++) at += set->extra[k];
    if (size) *size = set->extra[i];
    return set->payload + at;
}

static int frames_ok(const PeImpImageSet *set) {
    for (uint32_t i = 0; i < set->count; i++) {
        const uint8_t *f = pe_imp_imageset_frame(set, i, NULL);
        if (f[2] != 0x00 || f[3] != 0x38) return 0;             /* MDEC's 0x3800 */
    }
    return 1;
}

static FILE *open_set(const char *folder, const char *mode) {
    char path[PE_IMP_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/imageset.bin", folder) >= (int)sizeof path) return NULL;
    return fopen(path, mode);
}

static int bad(char *why, size_t n) {
    if (why && n) snprintf(why, n, "invalid or unreadable imageset.bin");
    return -1;
}

int pe_imp_imageset_save(const PeImpImageSet *set, const char *folder, char *why, size_t n) {
    size_t bytes = payload_size(set->format, set->width, set->height, set->count, set->size, set->extra);
    if (!bytes || !frames_ok(set)) return bad(why, n);
    FILE *f = open_set(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t header[20 + 8 * PE_IMP_IMAGESET_VIEWS] = {'I', 'M', 'G', '2'};
    put32(header + 4, set->format);
    put32(header + 8, set->width);
    put32(header + 12, set->height);
    put32(header + 16, set->count);
    for (uint32_t i = 0; i < set->count; i++) {
        put32(header + 20 + 4 * i, set->size[i]);
        put32(header + 20 + 4 * (set->count + i), set->extra[i]);
    }
    size_t hn = 20 + 8 * (size_t)set->count;
    int ok = fwrite(header, 1, hn, f) == hn && fwrite(set->payload, 1, bytes, f) == bytes;
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_imageset_load(const char *folder, PeImpImageSet **out, char *why, size_t n) {
    *out = NULL;
    FILE *f = open_set(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t header[20 + 8 * PE_IMP_IMAGESET_VIEWS];
    PeImpImageSet *set = NULL;
    uint32_t sizes[PE_IMP_IMAGESET_VIEWS], extra[PE_IMP_IMAGESET_VIEWS] = {0};
    if (fread(header, 20, 1, f) == 1 && !memcmp(header, "IMG", 3) && (header[3] == '1' || header[3] == '2')) {
        uint32_t count = get32(header + 16);
        int has_extra = header[3] == '2';
        if (count && count <= PE_IMP_IMAGESET_VIEWS && fread(header + 20, has_extra ? 8 : 4, count, f) == count) {
            for (uint32_t i = 0; i < count; i++) {
                sizes[i] = get32(header + 20 + 4 * i);
                if (has_extra) extra[i] = get32(header + 20 + 4 * (count + i));
            }
            set = pe_imp_imageset_alloc_extra(get32(header + 4), get32(header + 8), get32(header + 12), count, sizes, extra);
        }
    }
    int ok = 0;
    if (set) {
        size_t bytes = payload_size(set->format, set->width, set->height, set->count, set->size, set->extra);
        ok = fread(set->payload, 1, bytes, f) == bytes && fgetc(f) == EOF && !ferror(f) && frames_ok(set);
    }
    fclose(f);
    if (!ok) {
        free(set);
        return bad(why, n);
    }
    *out = set;
    return 0;
}
