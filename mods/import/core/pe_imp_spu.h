#ifndef PE_IMP_SPU_H
#define PE_IMP_SPU_H
#include "pe_imp.h"

enum { PE_IMP_SPU_BLOCK = 16, PE_IMP_SPU_FRAMES = 28 };

/* bytes / 16 * 28 frames into dst, ignoring loop flags; -1 on a bad block */
int pe_imp_spu_decode(const uint8_t *src, uint32_t bytes, int16_t *dst);

int pe_imp_spu_emit(PeImpSink *sink, const char *bank, unsigned wave,
                    const uint8_t *data, uint32_t bytes, char *why, size_t n);
int pe_imp_vab_read(PeImpSink *sink, const char *bank, const uint8_t *header, uint32_t header_size,
                    const uint8_t *body, uint32_t body_size, char *why, size_t n);
int pe_imp_vab_pair(const uint8_t *header, uint32_t header_size, const uint8_t *body, uint32_t body_size,
                    const char *path, PeImpSink *sink, char *why, size_t n);
int pe_imp_sound_bank_name(const char *path, char bank[24]);
#endif
