#ifndef PE1_ENTITY_ALLOCATION_H
#define PE1_ENTITY_ALLOCATION_H

#include "common.h"
#include "pe1/field_actor.h"

/* Allocation slots: a run length followed by its block's address. A zero address marks a free slot.
 * The first sixteen are the original ones, in the render scratch buffer. Native builds add more after them
 * in a buffer of their own (entity_overflow), used only once the sixteen are taken: a room the original
 * game fills keeps every model at its original address, and a room holding more (randomized enemies with
 * bigger models) gets them instead of Entity_AllocBlock returning NULL. */
typedef struct EntityAllocationBlock {
    u32 blockCount;
    uintptr_t address;
} EntityAllocationBlock;

#define ENTITY_ALLOCATION_ORIGINAL_SLOTS 16
#define ENTITY_ALLOCATION_SLOTS 64

extern EntityAllocationBlock g_EntityAllocationTable[ENTITY_ALLOCATION_SLOTS];
/* Bytes per allocation block in the render scratch buffer. */
/* Native packet links are wider than PS1 DMA words. Keep the original
 * sixteen-slot/1-2-4-8-block allocation policy with room for native packets. */
#define ENTITY_ALLOCATION_BLOCK_SIZE (0x47E0u * (sizeof(uintptr_t) / sizeof(u32)))

/* Fourteen original actor slots, stored at the native actor stride. */
extern FieldActor g_EntityPool[14];
extern int g_FieldMoveLock;

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
