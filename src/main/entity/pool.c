#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_lifetime.h"
#endif
#include "common.h"
#include <string.h>
#include "pe1/entity_allocation.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/script_node.h"
#include "pe1/script_queue.h"

int g_FieldMoveLock;
FieldActor *g_EntityFreeListHead;
extern FieldActor *g_FieldActorListHead;
FieldActor *g_CurrentEntity;
u16 g_EntityFreePoolCount;
FieldActor *g_PlayerEntity;
u32 g_EntitySerialCounter;


void Entity_ResetAllPools(void)
{
#if PE_NATIVE_STATE_CPU_COMPONENTS
    PeStateResource_Changed();
#endif
    unsigned int i;
    int *ptr;

    g_FieldMoveLock = 0;

    i = 0;
    ptr = (int *)g_ScriptVariableBank;
    do {
        *ptr = 0;
        i++;
        ptr++;
    } while (i < 0x200);

    i = 0;
    ptr = g_ScriptNodeActiveFlags;
    do {
        i++;
        *ptr = 0;
    } while (i < 0x40);

    memset(g_EntityPool, 0, sizeof(g_EntityPool));

    g_EntityFreeListHead = 0;
    g_FieldActorListHead = 0;
    g_CurrentEntity = 0;
    g_EntityFreePoolCount = 0;
    g_PlayerEntity = 0;
    g_EntitySerialCounter = 0;
    g_GameState.flags &= 0xFFFFCFFF;
}

void Entity_InitFreePool(void)
{
#if PE_NATIVE_STATE_CPU_COMPONENTS
    PeStateResource_Changed();
#endif
    unsigned i;
    g_EntityFreePoolCount = 0;
    g_FieldActorListHead = 0;
    g_EntityFreeListHead = g_EntityPool;
    for (i = 0; i < 13; ++i) g_EntityPool[i].next = &g_EntityPool[i + 1];
    g_EntityPool[13].next = 0;
    for (i = 0; i < 16; ++i) g_EntityAllocationTable[i].address = 0;
    g_PlayerEntity = 0;
}

/* Storage formerly supplied by the PS1 linker symbol map. */
FieldActor g_EntityPool[14];
EntityAllocationBlock g_EntityAllocationTable[16];
u32 g_ScriptVariableBank[512];
int g_ScriptNodeActiveFlags[64];
Pe1GameState g_GameState;

GameStateBlock g_GameStateBlock;
