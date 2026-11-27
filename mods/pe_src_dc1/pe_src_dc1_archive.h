#ifndef PE_SRC_DC1_ARCHIVE_H
#define PE_SRC_DC1_ARCHIVE_H

#include <stdint.h>

typedef struct {
    const uint8_t *data;
    uint32_t size, offset;
    unsigned index, count;
} Dc1Archive;

typedef struct {
    const uint8_t *data;
    uint32_t type, size, address, tag;
} Dc1Block;

/* Open returns 1 for a recognized archive and 0 for any other file. */
int dc1_archive_open(Dc1Archive *archive, const uint8_t *data, uint32_t size);
/* Next returns 1 for a block, 0 at the end, and -1 for a truncated block. */
int dc1_archive_next(Dc1Archive *archive, Dc1Block *block);

#endif
