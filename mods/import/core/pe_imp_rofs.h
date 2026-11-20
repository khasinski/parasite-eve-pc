#ifndef PE_IMP_ROFS_H
#define PE_IMP_ROFS_H
/* Resident Evil 3 PC (1999) rofs<N>.dat archives: the PC game's folder read like a disc image
 * (pe_imp_disc_open with any rofs*.dat path). Format and decryption as documented by Patrice
 * Mandin's reevengi-tools (rofs.c); see pe_imp_rofs.c. Read only, bounded, stdio. */
#include <stddef.h>
#include <stdint.h>

typedef struct { char name[32]; uint32_t offset, size; } PeImpRofsEntry;

/* 1 if the file name is rofs<digits>.dat (any case, any folder) */
int pe_imp_rofs_path(const char *path);
/* The archive's two folder names ("DATA", "SOUND") and its files; *out is malloc'd (free it). */
int pe_imp_rofs_list(const char *archive, char names[2][32], PeImpRofsEntry **out, int *count, char *why, size_t n);
/* One file, decrypted and unpacked, at the length its own header gives (the folder lists the
 * stored size: `size` only bounds nothing and is kept for the caller's records) into *data. */
int pe_imp_rofs_read(const char *archive, uint32_t offset, uint32_t size, uint8_t **data, uint32_t *got,
                     char *why, size_t n);
/* The parts on their own, for tests on made-up data. */
void pe_imp_rofs_decrypt(uint8_t *buf, uint32_t len, uint32_t key);
/* The archives' LZSS packing: up to `max` bytes out; returns the count written. */
uint32_t pe_imp_rofs_depack(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t max);

#endif
