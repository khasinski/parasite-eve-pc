#include "pe1/geom_state.h"
#include "common.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_animation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/game_state.h"
#include "pe1/gte.h"
#include "pe1/player_entity.h"
#include "pe1/pm.h"
#include "pe1/render_camera.h"
#include "pe1/render_setup.h"
#include "pe1/render_shadow.h"
#include "pe1/scene_archive.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room_load.h"
#include "pe1/scene_flags.h"
#include "pe1/script_node.h"
#include "pe1/script_queue.h"
#include "pe_actor_texture_pages.h"

/* Original USA per-type integration callbacks (800915DC). */
FieldActorCallbacks g_FieldActorTypeCallbacks[13] = {
    {Entity_IntegratePositionFull, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL},
    {Entity_IntegratePositionConditional, NULL}
};


FieldActor *Scene_LoadMap(u8 *scene, FieldActor *after, int allocateFull)
{
    FieldActor *actor;
    FieldActor *available;
    FieldActor *afterNext;
    FieldActor *nextFree;
    u8 *allocation;
    u8 *areaType;
    u32 entry;
    u32 i;
    u32 count;
    u32 value;
    u32 sceneType;
    u32 serial;
    void **row;
    u8 **table;
    SceneAssetBlob *archive;
    SceneAssetDirectory *directory;
    SceneBankAssetRecord *bankRows;
    SceneBankAssetRecord *walk;
    struct { u8 *primData; u32 unknown; u32 mode; } renderSetup;

    if (!scene || scene[0] >= sizeof(g_FieldActorTypeCallbacks) / sizeof(*g_FieldActorTypeCallbacks)) return NULL;
    available = g_EntityFreeListHead;
    if (!available) return 0;
    actor = available;
    nextFree = actor->next;
    g_EntityFreeListHead = nextFree;
    if (after) {
        afterNext = after->next;
        actor->prev = after;
        actor->next = afterNext;
        after->next = actor;
        if (actor->next) actor->next->prev = actor;
    } else {
        if (g_FieldActorListHead) g_FieldActorListHead->prev = actor;
        actor->prev = 0;
        actor->next = g_FieldActorListHead;
        g_FieldActorListHead = actor;
    }

    actor->gravity_x = 0;
    actor->gravity_y = 0x4000;
    actor->gravity_z = 0;
    actor->motion_x = 0;
    actor->motion_y = 0;
    actor->motion_z = 0;
    actor->accel_x = 0;
    actor->accel_y = 0;
    actor->accel_z = 0;
    actor->delta_x = 0;
    actor->delta_y = 0;
    actor->delta_z = 0;

    actor->frame_callback = g_FieldActorTypeCallbacks[scene[0]].frame;
    actor->contact_callback = g_FieldActorTypeCallbacks[scene[0]].contact;
    if (scene[0] == 0) {
        g_PlayerEntity = actor;
        actor->move_factor = 0x10000;
        Battle_InitEquipLists((struct AyaBattleState **)&actor->state);
    } else {
        actor->move_factor = 0x50000;
        actor->state = 0;
    }
    actor->type_id = scene[0];
    actor->sub_id = scene[1];
    if (scene[0] < 10) {
        actor->model_state.model_header = g_GameState.bank_slots[scene[0]];
    } else {
        actor->model_state.allocation_active = 0;
    }
    serial = g_EntitySerialCounter;
    actor->flags = 0x10000000;
    actor->anim_step = 0x10000;
    actor->pad_010 = 0xC8;
    actor->action_data = 0;
    actor->anim.fixed = 0;
    actor->parent = 0;
    actor->collision_face = 0;
    actor->script_cursor_19c = 0;
    actor->script_cursor_1a0 = 0;
    actor->pad_198 = 0;
    actor->move_speed = 0x1000;
    g_EntitySerialCounter = serial + 1;
    actor->field_sfx_id = serial;
    actor->mode = 0;
    ((RenderShadowActor *)actor)->shadow_matrix_index = 0;
    ((RenderShadowActor *)actor)->shadow_intensity = 0x80;
    for (i = 0; i < 0x38; ++i) actor->pad_0AC[i] = 0;
    for (i = 0; i < 3; ++i) actor->script_node_lists[i] = 0;

    table = g_GameState.bank_reset_944[0];
    actor->script_base = table[2 + scene[0]];
    actor->script_node_lists[2] = Script_AllocNode((unsigned int *)actor->script_base, 0);
    actor->rot_x = 0;
    actor->rot_y = 0;
    actor->rot_z = 0;
    g_EntityFreePoolCount++;

    if (actor->model_state.allocation_active) {
    if (actor == g_PlayerEntity) {
        Entity_SetActionMode((BattleEntity *)actor, 0x15);
    } else {
        void *(*rows)[48] = g_GameState.bank_rows;
        row = rows[actor->type_id];
        for (i = 0; i < 0x30; ++i) {
            if (row[i]) {
                Entity_SetActionMode((BattleEntity *)actor, i & 0xFFFF);
                break;
            }
        }
    }

    if (allocateFull) {
        allocation = Entity_AllocBlock(2 * (actor->model_state.model_header->packet34_count * sizeof(RenderPacket34) +
            actor->model_state.model_header->packet28_count * sizeof(RenderPacket28) +
            actor->model_state.model_header->packet24_count * sizeof(RenderPacket24) +
            actor->model_state.model_header->packet1c_count * sizeof(RenderPacket1C)) +
            actor->model_state.model_header->animation_entry_count * sizeof(RenderAnimationLookupEntry) +
            actor->model_state.model_header->part_count * sizeof(RenderMatrix) + 2 * sizeof(RenderShadowQuad));
    } else {
        allocation = Entity_AllocBlock(actor->model_state.model_header->animation_entry_count * sizeof(RenderAnimationLookupEntry) + actor->model_state.model_header->part_count * sizeof(RenderMatrix) + 2 * sizeof(RenderShadowQuad));
        actor->flags |= 0x600000A0;
    }
    actor->allocation_block = (uintptr_t)allocation;

    if (scene[0] == 0 && (areaType = &g_SceneAreaType)) {
        Render_SetupEntityPrims(&actor->render_object, actor->model_state.model_header, allocation + 2 * sizeof(RenderShadowQuad), 0x3C0,
                                0x100, 0, 0x1C0, 2, (s8 **)&renderSetup,
                                allocateFull);
    } else {
        archive = *g_LoadedSceneAssetBlock;
        directory = SceneAsset_ResolveOffset(archive, archive->directoryOffset);
        entry = directory->bankRootEntries;
        bankRows = SceneAsset_ResolveOffset(archive, entry & 0x3FFFFF);
        i = 0;
        if ((entry >> 22) != 0) {
            sceneType = scene[0];
            count = entry >> 22;
            walk = bankRows;
            /* The block-scope id keeps the match test at the top of the
             * loop (see docs/CODE_ORGANIZATION.md, loop exit tests). */
            do {
                u8 id = walk->source.bytes.id;

                if (id == sceneType) break;
                ++i;
                ++walk;
            } while (i < count);
        }

        allocation = (u8 *)actor->allocation_block;
        value = bankRows[i].trailing;
        Render_SetupEntityPrims(&actor->render_object, actor->model_state.model_header, allocation + 2 * sizeof(RenderShadowQuad),
                                (value >> 6) & 0x3C0, (value >> 9) & 0x180,
                                0, ((bankRows[i].trailing >> 18) & 0xFF) + 0x1C0,
                                (bankRows[i].trailing >> 8) & 0xF, (s8 **)&renderSetup,
                                allocateFull);
    }

    if (actor->action_data) {
        actor->render_object.shade = 0x80;
        actor->render_object.lightNegativeY = 0xC;
        actor->render_object.lightPositiveY = 0x18;
        Render_InitRoomPrimState(&actor->render_object);
        Render_DrawWithAnim(&actor->render_object, actor->action_data,
                            (s16)actor->anim.parts.integer, &g_RenderLightingState);
        actor->render_object.header->shadow_radius = actor->render_object.hit_cylinder.radius * 2;
    }
    } else {
        actor->flags |= 0xE0;
    }
    return actor;
}

/* Per-frame entity driver: callbacks, menu and battle hooks, camera, model
 * matrices, transform and draw, then animation and position updates. */
void Entity_FrameUpdate(void) {
    FieldActor *entity;
    FieldActor *focus;
    int camera[3];
    GteMatrix scale;

    if (!(g_GameStateBlock.flags & 4)) {
        for (entity = g_FieldActorListHead; entity != 0; entity = entity->next) {
            if (entity->frame_callback != 0) {
                entity->frame_callback(entity);
            }
        }
    }

    if (!(g_GameState.flags & 0x200)) {
        if (g_GameStateBlock.flags & 2) {
            Battle_Update();
        } else {
            if (g_PlayerEntity != 0 && g_PlayerEntity->state != 0 && (g_FieldPadPressed & 0x80) &&
                !(g_GameStateBlock.flags & 0x2000) && !(g_GameState.flags & 0x3400)) {
                Inv_OpenAyaItemList(0);
                Render_BeginSceneLoad();
                g_GameStateBlock.flags |= 4;
                g_GameState.flags |= 0x9000;
            }
            g_MenuFrameCommand = Menu_RunFrameWithArg(&g_BattleMenuActionRecord);
            if (g_MenuFrameCommand != 0) {
                Render_BeginSceneLoad();
                g_GameStateBlock.flags &= ~4;
                g_GameState.flags &= ~0x9000;
            }
        }

        Pm_RunBatch();
        if (!(g_GameStateBlock.flags & 4)) {
            Entity_UpdateList();
        }
        Entity_CopyParentPosition();
        Render_StepCameraTransition();

        focus = g_PlayerEntity;
        if (focus != 0) {
            camera[0] = focus->pos_x;
            camera[1] = (s32)((u32)focus->pos_y - ((u32)(s32)g_GeomScroll.state.position.focusHeight << 16));
            camera[2] = focus->pos_z;
        } else {
            camera[2] = 0;
            camera[1] = 0;
            camera[0] = 0;
            for (focus = g_FieldActorListHead; focus != 0; focus = focus->next) {
                if (focus->model_state.allocation_active != 0) {
                    camera[0] = focus->pos_x;
                    camera[1] = (s32)((u32)focus->pos_y - ((u32)(s32)g_GeomScroll.state.position.focusHeight << 16));
                    camera[2] = focus->pos_z;
                    break;
                }
            }
        }
        Render_SetViewport(camera);

        if (!(g_GameStateBlock.flags & 4)) {
            Render_SetGteScreenOffset();
            for (entity = g_FieldActorListHead; entity != 0; entity = entity->next) {
                if (!(entity->flags & 0x10040)) {
                    GteMatrix *matrix = (GteMatrix *)&entity->render_object.model_matrix;

                    matrix->t[0] = (s16)(entity->pos_x >> 16);
                    matrix->t[1] = (s16)(entity->pos_y >> 16);
                    matrix->t[2] = (s16)(entity->pos_z >> 16);
                    entity->render_object.table_value2c = entity->rot_x;
                    entity->render_object.table_value2e = entity->rot_y;
                    entity->render_object.table_value30 = entity->rot_z;
                    RotMatrix((GteShortVector *)&entity->render_object.table_value2c, matrix);
                    scale.m[0][0] = entity->move_speed;
                    scale.m[1][1] = entity->move_speed;
                    scale.m[2][2] = entity->move_speed;
                    scale.t[2] = 0;
                    scale.t[1] = 0;
                    scale.t[0] = 0;
                    scale.m[2][1] = 0;
                    scale.m[2][0] = 0;
                    scale.m[1][2] = 0;
                    scale.m[1][0] = 0;
                    scale.m[0][2] = 0;
                    scale.m[0][1] = 0;
                    gte_CompMatrix(matrix, &scale, matrix);
                    Pgxp_StoreModelMatrix(&entity->render_object.model_matrix,
                                          entity->rot_x, entity->rot_y, entity->rot_z,
                                          entity->move_speed);
                    Pgxp_StoreModelPosition16(&entity->render_object.model_matrix,
                                             entity->pos_x, entity->pos_y, entity->pos_z);
                }
            }
        }

        Render_SetGteScreenOffset();
        for (entity = g_FieldActorListHead; entity != 0; entity = entity->next) {
            RenderObjectEntity *object;

            if (entity == g_PlayerEntity && (g_GameState.flags & 0x40000)) {
                continue;
            }
            if (entity->model_state.allocation_active != 0) {
                entity->render_object.header->scale = entity->move_speed;
            }
            if (entity->flags & 0x40) {
                if (entity->flags & 0x2000) {
                    Render_TransformSkinnedVertices(&entity->render_object, g_EntityRenderScratch);
                    entity->pos_x = (s32)((u32)(s16)entity->render_object.rotation_overrides[0].x << 16);
                    entity->pos_y = (s32)((u32)(s16)entity->render_object.rotation_overrides[0].y << 16);
                    entity->pos_z = (s32)((u32)(s16)entity->render_object.rotation_overrides[0].z << 16);
                } else if (entity->model_state.allocation_active == 0) {
                    entity->render_object.hit_body.value0 = entity->pos_x >> 16;
                    entity->render_object.hit_body.value1 = entity->pos_y >> 16;
                    entity->render_object.hit_body.value2 = entity->pos_z >> 16;
                }
            } else {
                object = &entity->render_object;
                Render_InitRoomPrimState(object);
                if (entity->action_data != 0) {
                    Anim_BuildRotationMatrices(object, entity->action_data, (s16)entity->anim.parts.integer);
                }
                Render_TransformVertices(object);
                Render_TransformSkinnedVertices(object, g_EntityRenderScratch);
                if (!(entity->flags & 0x20000000)) {
                    Render_TransformMorphVertices(object, g_EntityRenderScratch);
                    PeModelTextures_Actor(entity);
                    Render_DrawEntity(object, g_EntityRenderScratch);
                    if (entity->flags & 0x8800) {
                        entity->render_object.flags_9C |= 1;
                    }
                    Render_DrawActorShadow((RenderShadowActor *)entity);
                }
            }
        }

        Scene_LoadEntityTextures();
        Pm_RunPrimary();
        Render_ResetGteScreenOffset();

        if (!(g_GameStateBlock.flags & 4)) {
            if (g_GameStateBlock.flags & 0x100) {
                if (!(g_GameState.flags & 0x40000) && g_PlayerEntity != 0) {
                    Entity_AdvanceAnim((BattleEntity *)g_PlayerEntity);
                }
            } else {
                for (entity = g_FieldActorListHead; entity != 0; entity = entity->next) {
                    if ((entity != g_PlayerEntity || !(g_GameState.flags & 0x40000)) &&
                        !(entity->flags & 0x800040)) {
                        Entity_AdvanceAnim((BattleEntity *)entity);
                    }
                }
            }
        }
    }

    if (!(g_GameStateBlock.flags & 4)) {
        Scene_UpdateEntityPositions();
        Script_GarbageCollectNodes();
        Entity_CollectGarbage();
        for (entity = g_FieldActorListHead; entity != 0; entity = entity->next) {
            entity->flags &= ~0x10000000;
        }
    }
}

/* Per-frame actor list passes: position integration, rollback, parent
 * following, garbage collection, callbacks and animation sequences. */
/* The actor list globals this unit owns. */
typedef FieldActor Entity;


void Entity_DispatchCallbacks(FieldActor *a);





/* Actor list maintenance after movement: rolling positions back through
 * the parent chain, following attached parents, freeing dead actors, and
 * running and advancing each actor's three script-node lists. */

void Entity_FreeAllocationBlock(uintptr_t arg0);
int Render_ReturnTrue(void *unused);
void Script_RunQueue(void);





void Entity_CollectGarbage(void) {
    Entity *cur;
    Entity *next;
    Entity *head_next;
    Entity *free_head;
    Entity *parent;
    int flags;

    cur = g_FieldActorListHead;
    if (cur != 0) {
        do {
            flags = cur->flags;
            parent = cur->parent;
            cur->flags = flags & ~0x800000;

            if (((parent != 0) && (parent->flags & 0x10)) || (flags & 0x10)) {
                Scene_FreeEntityTable(cur);
                next = cur->next;

                if (cur == g_PlayerEntity) {
                    g_PlayerEntity = 0;
                    g_FieldMoveLock &= ~0xD;
                }

                if (cur->model_state.allocation_active != 0) {
                    Entity_FreeAllocationBlock(cur->allocation_block);
                    Render_ReturnTrue(&cur->render_object);
                }

                if (cur->prev == 0) {
                    head_next = cur->next;
                    free_head = g_EntityFreeListHead;
                    g_EntityFreeListHead = cur;
                    g_FieldActorListHead = head_next;
                    if (head_next != 0) head_next->prev = 0;
                    cur->next = free_head;
                } else {
                    cur->prev->next = cur->next;
                    if (cur->next != 0) {
                        cur->next->prev = cur->prev;
                    }
                    cur->next = g_EntityFreeListHead;
                    g_EntityFreeListHead = cur;
                }

                g_EntityFreePoolCount--;
                cur = next;
            } else {
                cur = cur->next;
            }
        } while (cur != 0);
    }
}



void Entity_TickAnimSequences(FieldActor *arg0) {
    unsigned int i;
    ScriptNode *node;

    i = 0;
    do {
        node = arg0->script_node_lists[i];
        if (node != 0) {
            do {
                if (node->next_value != 0) {
                    node->current.value = node->next_value;
                    node->active = 1;
                    node->flags &= ~0x60;
                }
                node = node->next;
            } while (node != 0);
        }
        i++;
    } while (i < 3U);
}

FieldActor *g_FieldActorListHead;

void Entity_RollbackPositionHierarchy(FieldActor *arg0)
{
    FieldActor *cur;
    FieldActor *child;

    child = arg0->parent;
    if (child != 0) {
        Entity_RollbackPositionHierarchy(child);
        cur = g_FieldActorListHead;
        if (cur != 0) {
            do {
                if (cur->parent == arg0->parent) {
                    cur->pos_x = cur->base_x;
                    cur->pos_y = cur->base_y;
                    cur->pos_z = cur->base_z;
                    cur->collision_face = cur->collision_face_mirror;
                }
                cur = cur->next;
            } while (cur != 0);
        }
    } else {
        arg0->pos_x = arg0->base_x;
        arg0->pos_y = arg0->base_y;
        arg0->pos_z = arg0->base_z;
        arg0->collision_face = arg0->collision_face_mirror;
        arg0->flags |= 0x40000;
    }
}

void Entity_CopyParentPosition(void) {
    FieldActor *cur = g_FieldActorListHead;

    while (cur != 0) {
        if (cur->parent != 0 && (cur->flags & 0x400000) != 0) {
            cur->pos_x = cur->parent->pos_x;
            cur->pos_y = cur->parent->pos_y;
            cur->pos_z = cur->parent->pos_z;
            cur->rot_x = cur->parent->rot_x;
            cur->rot_y = cur->parent->rot_y;
            cur->rot_z = cur->parent->rot_z;
        }
        cur = cur->next;
    }
}

/* Original word additions wrap on MIPS. Avoid host signed-overflow UB. */
static s32 add_word(s32 lhs, s32 rhs)
{
    u32 value = (u32)lhs + (u32)rhs;
    return value <= INT32_MAX ? (s32)value : -1 - (s32)(UINT32_MAX - value);
}

void Entity_IntegratePositionFull(FieldActor *a)
{
    int tmp;
    int v0;

    a->base_x = a->pos_x;
    a->base_y = a->pos_y;
    a->base_z = a->pos_z;
    a->saved_rot_x = a->rot_x;
    a->saved_rot_y = a->rot_y;
    a->saved_rot_z = a->rot_z;
    Entity_DispatchCallbacks(a);

    if ((g_FieldMoveLock & 1) == 0) {
        v0 = g_PlayerEntity->state->flags;
        tmp = a->mode;
        if ((v0 & 0xC0) == 0x80) {
            tmp = 0x11;
        }
        Scene_CheckFlagBits(a, g_EntityInputRulesByMode, &tmp);
    }

    if (a->flags & 2) {
        a->motion_x = add_word(a->motion_x, a->gravity_x);
        a->motion_y = add_word(a->motion_y, a->gravity_y);
        a->motion_z = add_word(a->motion_z, a->gravity_z);
    }

    a->motion_x = add_word(a->motion_x, a->accel_x);
    a->motion_y = add_word(a->motion_y, a->accel_y);
    a->motion_z = add_word(a->motion_z, a->accel_z);

    a->pos_x = add_word(a->pos_x, a->motion_x);
    a->pos_y = add_word(a->pos_y, a->motion_y);
    a->pos_z = add_word(a->pos_z, a->motion_z);

    a->pos_x = add_word(a->pos_x, a->delta_x);
    a->pos_y = add_word(a->pos_y, a->delta_y);
    a->pos_z = add_word(a->pos_z, a->delta_z);
}

void Entity_IntegratePositionConditional(FieldActor *a)
{
    if (g_GameStateBlock.flags & 0x100) {
        Entity_DispatchCallbacks(a);
        return;
    }

    a->base_x = a->pos_x;
    a->base_y = a->pos_y;
    a->base_z = a->pos_z;
    a->saved_rot_x = a->rot_x;
    a->saved_rot_y = a->rot_y;
    a->saved_rot_z = a->rot_z;
    Entity_DispatchCallbacks(a);

    if (a->flags & 2) {
        a->motion_x = add_word(a->motion_x, a->gravity_x);
        a->motion_y = add_word(a->motion_y, a->gravity_y);
        a->motion_z = add_word(a->motion_z, a->gravity_z);
    }

    a->motion_x = add_word(a->motion_x, a->accel_x);
    a->motion_y = add_word(a->motion_y, a->accel_y);
    a->motion_z = add_word(a->motion_z, a->accel_z);

    a->pos_x = add_word(a->pos_x, a->motion_x);
    a->pos_y = add_word(a->pos_y, a->motion_y);
    a->pos_z = add_word(a->pos_z, a->motion_z);

    a->pos_x = add_word(a->pos_x, a->delta_x);
    a->pos_y = add_word(a->pos_y, a->delta_y);
    a->pos_z = add_word(a->pos_z, a->delta_z);
}

void Entity_DispatchCallbacks(FieldActor *arg0) {
    int i;
    ScriptNode **ptr;
    ScriptNode *callback;

    i = 0;
    ptr = arg0->script_node_lists;
    g_CurrentEntity = arg0;
    do {
        callback = *ptr;
        g_ScriptNodePool = callback;
        if (callback != 0) {
            Script_RunQueue();
        }
        i++;
        ptr++;
    } while ((unsigned int)i < 3U);
}
