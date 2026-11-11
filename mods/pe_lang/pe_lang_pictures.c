/* A translation's pictures: see pe_lang_pictures.h, and localization/petext/pictures.py for the
 * format (16-byte header "PEPX", version 1, piece count, bytes offset; 16-byte pieces sorted by
 * sector then offset; then the bytes). */
#include "pe_lang_pictures.h"

#include <stdlib.h>
#include <string.h>

#include "pe_lang_pack.h"

#define HEADER_SIZE 16u
#define PIECE_SIZE 16u
#define SECTOR_BYTES 2048u

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

int pe_lang_pictures_parse(PeLangPictures *pictures, unsigned char *data, size_t size) {
    memset(pictures, 0, sizeof *pictures);
    if (!data || size < HEADER_SIZE || size > PE_LANG_PICTURES_MAX || memcmp(data, "PEPX", 4) || rd32(data + 4) != 1)
        goto bad;
    uint32_t count = rd32(data + 8), bytes_at = rd32(data + 12);
    if (count > (size - HEADER_SIZE) / PIECE_SIZE || bytes_at != HEADER_SIZE + count * PIECE_SIZE) goto bad;
    uint64_t last = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char *p = data + HEADER_SIZE + i * PIECE_SIZE;
        uint32_t sector = rd32(p), start = rd32(p + 12);
        unsigned offset = rd16(p + 4), length = rd16(p + 6);
        uint64_t key = (uint64_t)sector << 16 | offset;
        if (!length || offset + length > SECTOR_BYTES || (i && key < last) ||
            start > size - bytes_at || length > size - bytes_at - start)
            goto bad;
        last = key;
    }
    pictures->data = data;
    pictures->size = size;
    pictures->count = count;
    return 0;
bad:
    free(data);
    return -1;
}

void pe_lang_pictures_free(PeLangPictures *pictures) {
    free(pictures->data);
    memset(pictures, 0, sizeof *pictures);
}

unsigned pe_lang_pictures_apply(const PeLangPictures *pictures, uint32_t sector, uint32_t count,
                                unsigned char *buffer) {
    unsigned applied = 0;
    if (!pictures || !pictures->count || !buffer || !count) return 0;
    const unsigned char *table = pictures->data + HEADER_SIZE;
    const unsigned char *bytes = pictures->data + rd32(pictures->data + 12);
    /* The first piece at or after `sector`. */
    uint32_t lo = 0, hi = pictures->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (rd32(table + mid * PIECE_SIZE) < sector) lo = mid + 1;
        else hi = mid;
    }
    for (uint32_t i = lo; i < pictures->count; ++i) {
        const unsigned char *p = table + i * PIECE_SIZE;
        uint32_t at = rd32(p);
        if (at - sector >= count) break;
        unsigned offset = rd16(p + 4), length = rd16(p + 6);
        unsigned char *target = buffer + (size_t)(at - sector) * SECTOR_BYTES + offset;
        /* Two pieces can share a place (two discs, two versions): the one whose English matches. */
        if (pe_lang_crc32(target, length) == rd32(p + 8)) {
            memcpy(target, bytes + rd32(p + 12), length);
            ++applied;
        }
    }
    return applied;
}
