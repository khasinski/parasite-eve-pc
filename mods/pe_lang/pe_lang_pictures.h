#ifndef PE_LANG_PICTURES_H
#define PE_LANG_PICTURES_H

/* A translation's pictures (pictures.bin in a zipped .pelang; localization/petext/pictures.py
 * writes it): pieces of PE.IMG sectors, each with the CRC-32 of the English bytes it replaces.
 * pe_lang puts them in as the sectors are read from the disc (PE_HOOK_DISC_READ), and only where
 * the bytes found have that CRC, so a piece never lands on another disc version. Plain data: no
 * SDL and no game state, so it is tested on its own. */

#include <stddef.h>
#include <stdint.h>

#define PE_LANG_PICTURES_MAX (64u << 20)   /* largest pictures.bin read */

typedef struct PeLangPictures {
    unsigned char *data; /* the whole pictures.bin, owned */
    size_t size;
    unsigned count;      /* pieces */
} PeLangPictures;

/* Take `data` (malloc'd, `size` bytes) after checking every piece in it. Returns 0 and owns the
 * data, or -1 and frees it: a damaged file is never half used. */
int pe_lang_pictures_parse(PeLangPictures *pictures, unsigned char *data, size_t size);
void pe_lang_pictures_free(PeLangPictures *pictures);

/* Put the pieces for sectors `sector` .. `sector + count - 1` into `buffer` (those sectors, 2048
 * bytes each), where the bytes there match. Returns how many pieces went in. */
unsigned pe_lang_pictures_apply(const PeLangPictures *pictures, uint32_t sector, uint32_t count,
                                unsigned char *buffer);

#endif
