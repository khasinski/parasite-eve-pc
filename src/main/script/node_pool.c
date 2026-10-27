#include "common.h"
#include <string.h>
#include <stdlib.h>
#include "pe1/entity_frame_update.h"
#include "pe1/psyq_gpu.h"
#include "pe1/field_actor.h"
#include "pe1/reloc_block.h"
#include "pe1/script_node.h"
#include "pe1/script_state.h"

/* Shared native state; external variables have one storage definition. */
ScriptNode *g_ScriptNodeFreeListHead;
unsigned short g_ScriptNodeSeqCounter[1];
RelocBlock *g_SceneDataTable1;
ScriptNode *g_ScriptNodePool;

/* Original task-node pool and scene relocation. */

void Script_InitNodePool(void) {
    unsigned int i;
    u32 *ptr;
    g_ScriptNodePool = 0;
    g_ScriptNodeSeqCounter[0] = 0;
    g_ScriptNodeFreeListHead = 0;

    memset(g_ScriptNodes, 0, sizeof(g_ScriptNodes));

    g_ScriptCursorAddress = 0;

    i = 0;
    ptr = g_ScriptOperandTable;
    while (i < 16) {
        *ptr++ = 0;
        i++;
    }

    g_SceneDataTable1 = 0;
}

void Script_InitNodeFreeList(void) {
    unsigned short i;

    i = 0;
    g_ScriptNodePool = 0;
    g_ScriptNodeFreeListHead = g_ScriptNodes;

    do {
        g_ScriptNodes[i].next = &g_ScriptNodes[i + 1];
        i++;
    } while (i < 0x47);

    g_ScriptNodes[71].next = 0;

    i = 0;
    do {
        g_ScriptNodeActiveFlags[i] = 0;
        i++;
    } while (i < 0x40);
}

ScriptNode *Script_AllocNode(unsigned int *script, ScriptNode *arg1) {
    ScriptNode *node;
    ScriptNode *next;
    unsigned short seq;
    unsigned short value;

    node = g_ScriptNodeFreeListHead;
    if (!node) return 0;
    next = node->next;
    g_ScriptNodeFreeListHead = next;

    if (arg1 != 0) {
        node->prev = arg1;
        next = arg1->next;
        node->next = next;
        if (next != 0) {
            next->prev = node;
        }
        arg1->next = node;
    } else {
        node->prev = 0;
        node->next = 0;
    }

    seq = g_ScriptNodeSeqCounter[0];
    value = 1;
    node->trigger_value = 0;
    node->current.script = script;
    node->next_value = 0;
    node->active = value;
    node->flags = 0;
    value = seq + 1;
    node->seq = seq;
    g_ScriptNodeSeqCounter[0] = value;

    return node;
}

void Script_GarbageCollectNodes(void) {
    FieldActor *group;
    u8 i;

    group = g_FieldActorListHead;
    if (group != 0) {
        do {
            i = 0;
            do {
                ScriptNode *node;
                ScriptNode *next;

                node = group->script_node_lists[i];
                if (node != 0) {
                    do {
                        next = node->next;
                        if ((group->flags & 0x10) || (node->flags & 0x10)) {
                            if (node->prev != 0) {
                                node->prev->next = next;
                            } else {
                                group->script_node_lists[i] = next;
                            }

                            if (node->next != 0) {
                                node->next->prev = node->prev;
                            }

                            node->next = g_ScriptNodeFreeListHead;
                            g_ScriptNodeFreeListHead = node;
                        }

                        node = next;
                    } while (node != 0);
                }

                i++;
            } while (i < 3);

            group = group->next;
        } while (group != 0);
    }
}

/* Original linker-allocated node and operand storage. */
ScriptNode g_ScriptNodes[72];
u32 g_ScriptOperandTable[16];

extern FieldActor *g_CurrentEntity;

int Script_SpawnChildNode(int **args) {
    ScriptNode *node = g_ScriptNodePool;

    if ((node->flags & 3) != 0) {
        int *value_ptr;
        FieldActor *state;
        ScriptNode *entry;
        u16 seq;
        unsigned int *script;
        u8 *base;
        int scriptValue;
        ScriptNode *next;

        value_ptr = args[0];
        state = g_CurrentEntity;
        entry = g_ScriptNodeFreeListHead;
        if (!entry) return 0;
        seq = g_ScriptNodeSeqCounter[0];
        scriptValue = *value_ptr;
        base = state->script_base;
        next = entry->next;
        entry->prev = 0;
        entry->next = 0;
        entry->trigger_value = 0;
        entry->next_value = 0;
        entry->active = 1;
        entry->seq = seq;
        entry->flags = 0;
        script = (unsigned int *)(base + (ptrdiff_t)scriptValue * 2);
        entry->current.script = script;
        {
            ScriptNode *head = state->script_node_lists[2];
            seq++;
            g_ScriptNodeSeqCounter[0] = seq;
            g_ScriptNodeFreeListHead = next;
            entry->next = head;
            if (head != 0) {
                head->prev = entry;
            }
            g_CurrentEntity->script_node_lists[2] = entry;
        }
    } else {
        ScriptNode *entry = g_ScriptNodeFreeListHead;
        int *value_ptr = args[0];
        FieldActor *state = g_CurrentEntity;
        ScriptNode *next;
        if (!entry) return 0;
        next = entry->next;
        uintptr_t value;

        value = (uintptr_t)(state->script_base + (ptrdiff_t)*value_ptr * 2);
        g_ScriptNodeFreeListHead = next;
        if (node != 0) {
            entry->prev = node;
            next = node->next;
            entry->next = next;
            if (next != 0) {
                next->prev = entry;
            }
            node->next = entry;
        } else {
            entry->prev = 0;
            entry->next = 0;
        }
        entry->current.script = (unsigned int *)value;
        {
            u16 seq = g_ScriptNodeSeqCounter[0];
            entry->trigger_value = 0;
            entry->next_value = 0;
            entry->active = 1;
            entry->flags = 0;
            g_ScriptNodeSeqCounter[0] = seq + 1;
            entry->seq = seq;
        }
    }
    return 1;
}

RelocBlock *Script_RelocBlock(void *block, size_t size) {
    static RelocBlock *resolved;
    u8 *bytes = block;
    u32 offset, count;
    if (!block) return NULL;
    if (block == resolved) {
        g_SceneDataTable1 = resolved;
        return resolved;
    }
    if (size < 8) return NULL;
    memcpy(&offset, bytes, sizeof(offset));
    memcpy(&count, bytes + 4, sizeof(count));
    if (offset >= size || count > (size - 8) / 4) return NULL;
    for (u32 i = 0; i < count; ++i) {
        u32 entry;
        memcpy(&entry, bytes + 8 + (size_t)i * 4, sizeof(entry));
        if (entry >= size) return NULL;
    }
#if UINTPTR_MAX <= UINT32_MAX
    if (count > (SIZE_MAX - sizeof(RelocBlock)) / sizeof(u8 *)) return NULL;
#endif
    RelocBlock *next = malloc(sizeof(RelocBlock) + (size_t)count * sizeof(u8 *));
    if (!next) return NULL;
    next->u0.base = bytes + offset;
    next->count = count;
    for (u32 i = 0; i < count; ++i) {
        memcpy(&offset, bytes + 8 + (size_t)i * 4, sizeof(offset));
        next->offsets[i] = bytes + offset;
    }
    free(resolved);
    resolved = next;
    g_SceneDataTable1 = resolved;
    return resolved;
}

void Script_DrawSyncAndFlush(void) {
    unsigned int i;
    int offset;
    RelocBlock *block;

    DrawSync(0);
    block = g_SceneDataTable1;
    i = (unsigned char)block->u0.base[0] != 0;
    if (i != 0) {
        i = 0;
        offset = 1;
        do {
            Scene_LoadMap(block->u0.base + offset, 0, 1);
            block = g_SceneDataTable1;
            i++;
            offset += 2;
        } while (i < (unsigned char)block->u0.base[0]);
    }
}
