#ifndef PE1_RELOC_BLOCK_H
#define PE1_RELOC_BLOCK_H
#include "common.h"

/* Native view of a disk block: base offset, count, then count 32-bit offsets.
 * The offset words stay in the asset; resolved pointers live in this view. */
typedef struct RelocBlock {
    union { u8 *base; } u0;
    uintptr_t count;
    u8 *offsets[];
} RelocBlock;
PE1_STATIC_ASSERT(offsetof(RelocBlock, offsets) == 2 * sizeof(void *), reloc_pointer_table_offset);
extern RelocBlock *g_SceneDataTable1;
RelocBlock *Script_RelocBlock(void *block, size_t bytes);
void Script_DrawSyncAndFlush(void);
#endif
