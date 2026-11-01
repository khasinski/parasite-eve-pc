#ifndef PE_LANG_CJK_H
#define PE_LANG_CJK_H
#include "pe_lang_pack.h"
#define PE_CJK_CACHE_SIZE 191
typedef struct PeCjkSlot { unsigned id; unsigned short cell; unsigned char valid, pinned; } PeCjkSlot;
typedef struct PeCjkCache {
    const PeLangPack *pack;
    PeCjkSlot slots[PE_CJK_CACHE_SIZE];
    unsigned count, hand;
    int need_sync, overflow;
} PeCjkCache;
typedef void (*PeCjkSync)(void *);
typedef int (*PeCjkUpload)(unsigned cell, const unsigned char *pixels, void *);
void pe_cjk_reset(PeCjkCache *,const PeLangPack *,const unsigned char *original);
void pe_cjk_frame(PeCjkCache *);
/* Returns 1 for an uploaded/hit glyph, 0 if all eligible cells are pinned or upload fails.
 * Never evict a cell referenced by a packet in the frame being built. */
int pe_cjk_resolve(PeCjkCache *,unsigned id,unsigned *cell,PeCjkSync,PeCjkUpload,void *);
#endif
