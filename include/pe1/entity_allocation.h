#ifndef PE1_ENTITY_ALLOCATION_H
#define PE1_ENTITY_ALLOCATION_H

#include "common.h"
#include "pe1/field_actor.h"
#include "pe1/render_shadow.h"

/* Sixteen allocation slots: a run length followed by its scratch-buffer address.
 * A zero address marks a free slot. */
typedef struct EntityAllocationBlock {
    u32 blockCount;
    uintptr_t address;
} EntityAllocationBlock;



extern EntityAllocationBlock g_EntityAllocationTable[16];
/* Bytes per allocation block in the render scratch buffer. */
/* Native packet links are wider than PS1 DMA words. Keep the original
 * sixteen-slot/1-2-4-8-block allocation policy with room for native packets. */
#define ENTITY_ALLOCATION_BLOCK_SIZE (0x47E0u * (sizeof(uintptr_t) / sizeof(u32)))

/* Fourteen original actor slots, stored at the native actor stride. */
extern FieldActor g_EntityPool[14];
extern int g_FieldMoveLock;

/* Per-actor writable storage for a model. Packets and shadows are double
 * buffered; animation entries and pose matrices are shared between frames. */
static inline u32 Entity_ModelAllocationSize(const RenderObjectHeader *model, int withPackets) {
    size_t bytes = 2 * sizeof(RenderShadowQuad) +
                   model->animation_entry_count * sizeof(RenderAnimationLookupEntry) +
                   model->part_count * sizeof(RenderMatrix);
    if (withPackets) {
        bytes += 2 * (model->packet34_count * sizeof(RenderPacket34) +
                      model->packet28_count * sizeof(RenderPacket28) +
                      model->packet24_count * sizeof(RenderPacket24) +
                      model->packet1c_count * sizeof(RenderPacket1C));
    }
    return (u32)bytes;
}

/* Claims scratch-buffer slots for a model allocation of `size` bytes and
 * returns its address. */
void *Entity_AllocBlock(u32 size);
void Entity_FreeAllocationBlock(uintptr_t address);
void Entity_ResetAllPools(void);
void Entity_InitFreePool(void);

/* Field entity pool (entity/pool.c): the free-record list, the number
 * of live records and the serial number of the next allocated entity. */
struct FieldActor;
extern struct FieldActor *g_EntityFreeListHead;
extern u16 g_EntityFreePoolCount;
extern u32 g_EntitySerialCounter;

#endif
