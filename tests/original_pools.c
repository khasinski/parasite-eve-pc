#include "pe1/entity_allocation.h"
#include "pe1/game_state.h"
#include "pe1/render_buffers.h"
#include "pe1/script_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    unsigned char *memory=malloc(ENTITY_ALLOCATION_ORIGINAL_SLOTS*ENTITY_ALLOCATION_BLOCK_SIZE);
    FieldActor *walk;
    void *first,*second,*third;
    unsigned count=0;
    if (!memory) return 1;
    memset(g_EntityPool,0xaa,sizeof(g_EntityPool));
    for (unsigned i=0;i<512;++i) g_ScriptVariableBank[i]=i+1;
    g_GameState.flags=0xffffffff;
    Entity_ResetAllPools(); Entity_InitFreePool();
    for (walk=g_EntityFreeListHead;walk;walk=walk->next) {
        if (walk!=&g_EntityPool[count] || walk->pos_x || walk->shadow_intensity) return 2;
        if (++count>14) return 3;
    }
    if (count!=14 || g_EntityFreePoolCount || g_GameState.flags!=0xffffcfff) return 4;
    for (unsigned i=0;i<512;++i) if (g_ScriptVariableBank[i]) return 5;
    g_RenderScratchBufferBase[0]=memory;
    first=Entity_AllocBlock(1); second=Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE+1);
    third=Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE*4+1);
    if (first!=memory || second!=memory+2*ENTITY_ALLOCATION_BLOCK_SIZE ||
        third!=memory+8*ENTITY_ALLOCATION_BLOCK_SIZE) return 6;
    void *overflow[ENTITY_ALLOCATION_SLOTS / 8];
    for (unsigned i=ENTITY_ALLOCATION_ORIGINAL_SLOTS;i<ENTITY_ALLOCATION_SLOTS;i+=8) {
        overflow[i/8]=Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE*4+1);
        if (!overflow[i/8] || (uintptr_t)overflow[i/8]!=g_EntityAllocationTable[i].address ||
            g_EntityAllocationTable[i].blockCount!=8) return 7;
        memset(overflow[i/8],i,8*ENTITY_ALLOCATION_BLOCK_SIZE);
    }
    for (unsigned i=ENTITY_ALLOCATION_ORIGINAL_SLOTS;i<ENTITY_ALLOCATION_SLOTS;i+=8)
        if (((unsigned char *)overflow[i/8])[0]!=i) return 7;
    if (Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE*4+1)) return 7;
    Entity_FreeAllocationBlock((uintptr_t)second);
    if (Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE+1)!=second) return 8;
    Entity_InitFreePool();
    for (unsigned i=0;i<ENTITY_ALLOCATION_SLOTS;i+=8)
        if (Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE*4+1)!=
            (i<ENTITY_ALLOCATION_ORIGINAL_SLOTS ? memory+i*ENTITY_ALLOCATION_BLOCK_SIZE : overflow[i/8])) return 9;
    if (Entity_AllocBlock(1)) return 9;
    g_RenderScratchBufferBase[0]=0; free(memory);
    if (Entity_AllocBlock(1)) return 10;
    puts("Original 14-actor pool and 1/2/4/8-block allocator passed");
    return 0;
}
