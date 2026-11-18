#include <string.h>
#include "pe_src_dc1_archive.h"

enum { SECTOR = 2048, ENTRY = 16 };

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int dc1_archive_open(Dc1Archive *archive, const uint8_t *data, uint32_t size)
{
    if (size < SECTOR) return 0;
    for (unsigned i = 0; i < SECTOR / ENTRY; ++i) {
        if (!memcmp(data + i * ENTRY, "dummy header", 12)) {
            *archive = (Dc1Archive){data, size, SECTOR, 0, i};
            return 1;
        }
    }
    return 0;
}

int dc1_archive_next(Dc1Archive *archive, Dc1Block *block)
{
    if (archive->index == archive->count) return 0;

    const uint8_t *entry = archive->data + archive->index * ENTRY;
    uint32_t size = le32(entry + 4), offset = archive->offset;
    if (offset > archive->size || size > archive->size - offset) return -1;

    *block = (Dc1Block){archive->data + offset, le32(entry), size, le32(entry + 8), le32(entry + 12)};
    /* A final payload can end before its sector padding. The next block still
     * has to be within the archive. */
    uint64_t next = ((uint64_t)offset + size + SECTOR - 1) / SECTOR * SECTOR;
    archive->offset = next > UINT32_MAX ? UINT32_MAX : (uint32_t)next;
    ++archive->index;
    return 1;
}
