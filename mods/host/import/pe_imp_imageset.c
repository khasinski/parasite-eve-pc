/* Portable ImageSet store format (imageset.bin): "IMG1", u32 format, width, height, count, then
 * count u32 frame sizes, then the frames. Little-endian, bounded. */
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

static size_t payload_size(uint32_t format, uint32_t w, uint32_t h, uint32_t count, const uint32_t *sizes) {
    if (format != PE_IMP_IMAGESET_MDEC || !w || !h || w % 16 || h % 16 || w > 1024 || h > 1024 || !count ||
        count > PE_IMP_IMAGESET_VIEWS)
        return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (sizes[i] < 12) return 0;
        total += sizes[i];
    }
    return total <= PE_IMP_IMAGESET_MAX ? (size_t)total : 0;
}

PeImpImageSet *pe_imp_imageset_alloc(uint32_t format, uint32_t w, uint32_t h, uint32_t count, const uint32_t *sizes) {
    size_t bytes = payload_size(format, w, h, count, sizes);
    if (!bytes) return NULL;
    PeImpImageSet *set = calloc(1, sizeof *set + bytes);
    if (set) {
        set->format = format, set->width = w, set->height = h, set->count = count;
        memcpy(set->size, sizes, count * sizeof *sizes);
    }
    return set;
}

const uint8_t *pe_imp_imageset_frame(const PeImpImageSet *set, uint32_t i, uint32_t *size) {
    if (i >= set->count) return NULL;
    size_t at = 0;
    for (uint32_t k = 0; k < i; k++) at += set->size[k];
    if (size) *size = set->size[i];
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
    size_t bytes = payload_size(set->format, set->width, set->height, set->count, set->size);
    if (!bytes || !frames_ok(set)) return bad(why, n);
    FILE *f = open_set(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t header[20 + 4 * PE_IMP_IMAGESET_VIEWS] = {'I', 'M', 'G', '1'};
    put32(header + 4, set->format);
    put32(header + 8, set->width);
    put32(header + 12, set->height);
    put32(header + 16, set->count);
    for (uint32_t i = 0; i < set->count; i++) put32(header + 20 + 4 * i, set->size[i]);
    size_t hn = 20 + 4 * (size_t)set->count;
    int ok = fwrite(header, 1, hn, f) == hn && fwrite(set->payload, 1, bytes, f) == bytes;
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_imageset_load(const char *folder, PeImpImageSet **out, char *why, size_t n) {
    *out = NULL;
    FILE *f = open_set(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t header[20 + 4 * PE_IMP_IMAGESET_VIEWS];
    PeImpImageSet *set = NULL;
    uint32_t count = 0, sizes[PE_IMP_IMAGESET_VIEWS];
    if (fread(header, 20, 1, f) == 1 && !memcmp(header, "IMG1", 4)) {
        count = get32(header + 16);
        if (count && count <= PE_IMP_IMAGESET_VIEWS && fread(header + 20, 4, count, f) == count) {
            for (uint32_t i = 0; i < count; i++) sizes[i] = get32(header + 20 + 4 * i);
            set = pe_imp_imageset_alloc(get32(header + 4), get32(header + 8), get32(header + 12), count, sizes);
        }
    }
    int ok = 0;
    if (set) {
        size_t bytes = payload_size(set->format, set->width, set->height, set->count, set->size);
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

static uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

int pe_imp_emit_mdec_slots(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                           uint32_t slot, uint32_t w, uint32_t h) {
    uint32_t sizes[PE_IMP_IMAGESET_VIEWS], at[PE_IMP_IMAGESET_VIEWS], count = 0;
    if (!slot) return -1;
    for (uint32_t o = 0; o + 12 <= size && count < PE_IMP_IMAGESET_VIEWS; o += slot) {
        const uint8_t *f = data + o;
        uint32_t version = rd16(f + 6);
        if (!rd16(f) || rd16(f + 2) != 0x3800 || (version != 2 && version != 3)) continue;
        uint32_t len = size - o < slot ? size - o : slot;
        while (len > 12 && !f[len - 1]) len--;              /* the slot's padding */
        len = (len + 3) & ~3u;
        if (o + len > size) len = size - o;
        at[count] = o, sizes[count++] = len;
    }
    if (!count) return 1;                                   /* no pictures: not an error */
    PeImpImageSet *set = pe_imp_imageset_alloc(PE_IMP_IMAGESET_MDEC, w, h, count, sizes);
    if (!set) return -1;
    uint8_t *p = set->payload;
    for (uint32_t i = 0; i < count; i++) memcpy(p, data + at[i], sizes[i]), p += sizes[i];
    return pe_imp_emit(sink, PE_IMP_IMAGESET, id, name, set) ? -1 : 0;
}
