#include <string.h>
#include "pe_hooks.h"
#include "common.h"
#include "pe1/battle_context.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_update.h"
#include "pe1/boot.h"
#include "pe1/collision_database.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_animation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/field_movement.h"
#include "pe1/font.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/geom_state.h"
#include "pe1/gte_types.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_inventory.h"
#include "pe1/player_entity.h"
#include "pe1/pm.h"
#include "pe1/render_camera.h"
#include "pe1/render_setup.h"
#include "pe1/save.h"
#include "pe1/save_tail_state.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_transition.h"
#include "pe1/script_anim.h"
#include "pe1/script_node.h"
#include "pe1/script_state.h"
#include "pe1/textbox.h"

/* Shared native state; external variables have one storage definition. */

int Script_EncodeBarcode(int **arg0) {
    char tmp[8];
    int value;
    unsigned int *counter;

    Text_EncodeBase32(tmp, *arg0[0]);
    value = *arg0[0];
    g_GameStateFlags |= 0x2000;
    g_SceneDispatchToken = value;

    if (*arg0[0] == 0xA9400048) {
        counter = &g_PlayTimeFrameCounter;
        if (*counter == 0x7D0) {
            *counter = 0;
        } else {
            Boot_Shutdown();
        }
    }
    return 0;
}


int Script_SetFogParams(int **arg0) {
    Render_SetScrollMode((s16)*arg0[0], (s16)*arg0[1], (u16)*arg0[2], 0);
    return 1;
}

int Script_SetFogParamsAlt(int **arg0) {
    Render_SetScrollMode((s16)*arg0[0], (s16)*arg0[1], (u16)*arg0[2], 8);
    return 1;
}

int Script_IsBattlePhase4(int **arg0) {
    *arg0[0] = ((g_GeomScroll.state.flags & 7) == 4);
    return 1;
}

int Script_ResetPlayerCameraPos(int **unused) {
    (void)unused;
    Render_UpdateScrollPosition(&g_PlayerEntity->pos_x, -1, -1);
    return 1;
}

int Script_SetBattleStateMode(int **unused) {
    (void)unused;
    int *ptr = &g_GeomScroll.state.flags;

    *ptr = (*ptr & -8) | 0x80;
    return 1;
}

int Script_SetEntityField224(int **arg0) {
    FieldActor *entity;
    int value;

    entity = g_CurrentEntity;
    value = (s16)((u32)*arg0[0] >> 16);
    entity->render_object.hit_cylinder.radius = value;
    return 1;
}

int Entity_ClearGlobalFlag1(int **unused) {
    (void)unused;
    g_FieldMoveLock &= -2;
    return 1;
}

int Entity_SetGlobalFlag1(int **unused) {
    (void)unused;
    g_FieldMoveLock |= 1;
    return 1;
}

int Entity_SetCurrentFlag40(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x40;
    return 1;
}

int Entity_ClearCurrentFlag40(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= -0x41;
    return 1;
}

/* Script opcodes: read the menu choice and branch the actor script on
 * inequality. Contiguous pair; the choice reader has no data, so the
 * branch's assembler options leave it unchanged. */


int Script_GetMenuChoice(int **arg0) {
    int value;

    value = Menu_GetEquipSlotIndex();
    **arg0 = (signed char)value;
    return 1;
}

int Script_CondBranch(int **arg0) {
    int *a;
    int *b;
    int *c;

    a = arg0[0];
    b = arg0[1];
    if (*a != *b) {
        c = arg0[2];
        g_ScriptCursorAddress = (uintptr_t)g_CurrentEntity->script_base +
            (intptr_t)(s32)((u32)*c << 1);
    }
    return 1;
}

/* Contiguous current-actor flag and animation opcodes. */

int Entity_TestCurrentFlags(int **arg0) {
    FieldActor *entity;
    int flags;
    int mask;

    entity = g_CurrentEntity;
    flags = entity->flags;
    mask = *arg0[0];
    *arg0[1] = (flags & mask) == mask;
    return 1;
}

int Script_Noop(int **unused) {
    (void)unused;
    return 1;
}

int Script_SetEntityField1C(int **arg0) {
    g_CurrentEntity->anim_step = *arg0[0];
    return 1;
}

int Script_SetEntityAnimSpeed(int **arg0) {
    FieldActor *entity;
    unsigned int value;

    entity = g_CurrentEntity;
    value = (u16)**arg0;
    if (entity->action < value) {
        value = entity->action;
    }
    entity->anim.fixed = value << 16;
    return 1;
}

int Entity_SetCurrentFlag100(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x100;
    return 1;
}

int Entity_ClearCurrentFlag100(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= -0x101;
    return 1;
}

/* Script opcodes: test, set and clear the global game-state flags, set the
 * battle mode state and read a battle context field of the current actor.
 * Contiguous default-profile handlers. */

int Scene_TestGlobalFlags(int **arg0) {
    int flags;
    int mask;

    flags = g_GameStateFlags;
    mask = *arg0[0];
    if (flags & mask) {
        *arg0[1] = 1;
    } else {
        *arg0[1] = 0;
    }
    return 1;
}

int Scene_SetGlobalFlags(int **arg0) {
    g_GameStateFlags |= *arg0[0];
    return 1;
}

int Scene_ClearGlobalFlags(int **arg0) {
    g_GameStateFlags &= ~*arg0[0];
    return 1;
}

int Menu_SetModeState5(int **unused) {
    (void)unused;
    g_BattleModeState = 5;
    return 1;
}

int Menu_SetModeState6(int **unused) {
    (void)unused;
    g_BattleModeState = 6;
    return 1;
}

int Entity_GetField(int **arg0) {
    FieldActor *ctx = g_CurrentEntity;

    if (ctx->type_id == 0) {
        *arg0[1] = Battle_GetContextField((u8)*arg0[0]);
    } else {
        *arg0[1] = Battle_GetEnemyContextField(ctx, (u8)*arg0[0]);
    }
    return 1;
}

int Entity_FindByTypeId(int **arg0) {
    FieldActor *obj;
    int type;
    int cmpType;

    type = *arg0[0];
    if (type == 0) {
        *arg0[3] = Battle_GetContextField(*(u8 *)arg0[2]);
    } else {
        obj = g_FieldActorListHead;
        cmpType = type;
        if (obj == 0) {
            return 1;
        }
        do {
            if (obj->type_id != cmpType) {
                obj = obj->next;
            } else if (obj->sub_id != *arg0[1]) {
                obj = obj->next;
            } else if ((obj->flags & 0x10) != 0) {
                obj = obj->next;
            } else {
                break;
            }
        } while (obj != 0);
        if (obj == 0) {
            return 1;
        }
        *arg0[3] = Battle_GetEnemyContextField(obj, *(u8 *)arg0[2]);
    }
    return 1;
}

int Entity_SetField(int **arg0) {
    FieldActor *ctx = g_CurrentEntity;

    if (ctx->type_id == 0) {
        Battle_SetContextField((u8)*arg0[0], *arg0[1]);
    } else {
        Entity_WriteFieldByCmd(ctx, (u8)*arg0[0], *arg0[1]);
    }

    return 1;
}

int Entity_SetFieldByTypeId(int **arg0) {
    FieldActor *obj;
    int type;
    int cmpType;

    type = *arg0[0];
    if (type == 0) {
        Battle_SetContextField(*(u8 *)arg0[2], *arg0[3]);
    } else {
        obj = g_FieldActorListHead;
        cmpType = type;
        if (obj == 0) {
            return 1;
        }
        do {
            if (obj->type_id != cmpType) {
                obj = obj->next;
            } else if (obj->sub_id != *arg0[1]) {
                obj = obj->next;
            } else if ((obj->flags & 0x10) != 0) {
                obj = obj->next;
            } else {
                break;
            }
        } while (obj != 0);
        if (obj != 0) {
            Entity_WriteFieldByCmd(obj, *(u8 *)arg0[2], *arg0[3]);
        }
    }
    return 1;
}

int Script_ClearBattleFlags(int **unused) {
    (void)unused;
    int *ptr = &g_GeomScroll.state.flags;

    *ptr &= -0xC1;
    return 1;
}

int Script_SetBattleFlags(int **unused) {
    (void)unused;
    int *ptr = &g_GeomScroll.state.flags;

    *ptr |= 0xC0;
    return 1;
}

int Script_SetCurrentEntityField20(int **arg0) {
    g_CurrentEntity->move_factor = *arg0[0];
    return 1;
}

int Entity_MarkAnimPending(int **unused) {
    unsigned int i;
    FieldActor *entity;
    ScriptNode *node;
    ScriptNode *skip;

    i = 0;
    skip = g_ScriptNodePool;
    entity = g_CurrentEntity;
    do {
        node = entity->script_node_lists[i];
        if (node != 0) {
            do {
                if (node != skip) {
                    node->flags |= 0x40;
                }
                node = node->next;
            } while (node != 0);
        }
        i++;
    } while (i < 3U);
    return 1;
}

int Entity_AdvancePendingAnim(int **unused) {
    unsigned int i;
    FieldActor *entity;
    ScriptNode *node;
    int active;

    i = 0;
    active = 1;
    entity = g_CurrentEntity;
    do {
        node = entity->script_node_lists[i];
        if (node != 0) {
            do {
                if (node->flags & 0x40) {
                    node->flags &= ~0x40;
                    if (node->next_value != 0) {
                        node->current.value = node->next_value;
                        node->active = active;
                        node->flags &= ~0x20;
                    }
                }
                node = node->next;
            } while (node != 0);
        }
        i++;
    } while (i < 3U);
    return 1;
}

int Entity_MarkNodeAnimPendingById(int **arg0) {
    unsigned int i;
    FieldActor *entity;
    ScriptNode *node;

    i = 0;
    entity = g_CurrentEntity;
    do {
        node = entity->script_node_lists[i];
        if (node != 0) {
            do {
                if (node->seq == **arg0) {
                    node->flags |= 0x40;
                    return 1;
                }
                node = node->next;
            } while (node != 0);
        }
        i++;
    } while (i < 3U);

    return 1;
}

int Entity_AdvanceNodeById(int **arg0) {
    unsigned int i;
    FieldActor *entity;
    ScriptNode *node;
    intptr_t next_value;
    int active_value;

    i = 0;
    active_value = 1;
    entity = g_CurrentEntity;
    do {
        node = entity->script_node_lists[i];
        if (node != 0) {
            do {
                if (node->seq == **arg0) {
                    node->flags &= ~0x40;
                    next_value = node->next_value;
                    if (next_value != 0) {
                        node->current.value = next_value;
                        node->active = active_value;
                        node->flags &= ~0x20;
                    }
                }
                node = node->next;
            } while (node != 0);
        }
        i++;
    } while (i < 3U);

    return 1;
}

int Script_RunEntityCommand(int **arg0) {
    int result;

    result = (signed char)Entity_TriggerAnimEvent((BattleEntity *)g_CurrentEntity, *(unsigned char *)arg0[0]);
    *arg0[1] = result;
    if (*arg0[1] != 0) {
        return 1;
    }

    g_ScriptNodePool->active = 1;
    g_ScriptCursorAddress -= 0x10;
    return 0;
}

/* Script opcodes from 0x8001856C to 0x80018FDC: motion reset and velocity,
 * combat timer and mode flags, process-manager commands, actor animation and
 * collision helpers, object entry setters, camera/CD/fog settings and actor
 * move speed. All are default-profile handlers that reach the current actor
 * through absolute addresses. */

int Entity_ClearMotionVectors(int **unused) {
    (void)unused;
    g_CurrentEntity->motion_x = 0;
    g_CurrentEntity->motion_y = 0;
    g_CurrentEntity->motion_z = 0;
    g_CurrentEntity->accel_x = 0;
    g_CurrentEntity->accel_y = 0;
    g_CurrentEntity->accel_z = 0;
    return 1;
}

int Entity_ComputeVelocity(int **unused) {
    (void)unused;
    FieldActor *state = g_CurrentEntity;
    int temp;

    if (state == g_PlayerEntity) {
        temp = Math_FixedMul(0x50000, state->move_factor);
    } else {
        temp = state->move_factor;
    }

    temp = Math_FixedMul(temp, g_CurrentEntity->move_speed << 4);
    g_CurrentEntity->motion_x = Math_FixedMul((s32)(0u-(u32)temp), (s32)((u32)rsin(g_CurrentEntity->rot_y) << 4));
    g_CurrentEntity->motion_z = Math_FixedMul((s32)(0u-(u32)temp), (s32)((u32)rcos(g_CurrentEntity->rot_y) << 4));

    return 1;
}

/* Camera base angles at 0x800BD020 (g_GeomScroll.state.position.baseAngleDigital, g_GeomScroll.state.position.baseAngleAnalog). The script
 * opcode stores them as one record, so the second argument load stays behind
 * the first store: GCC 2.7 lets a varying array load pass a store to a fixed
 * scalar, but not to a structure member. */
typedef struct CameraBaseAngles {
    short x;
    short y;
} CameraBaseAngles;

int Script_ComputeAudioTimer(int **arg0) {
    int *dst;
    u32 value;

    dst = &g_GameTimers[3].current;
    value = ((u32)*arg0[0] + (u32)*arg0[1]) * 216000u;
    value += (u32)*arg0[2] * 60u;
    *dst = (s32)value;
    if (*arg0[3] == 1) {
        g_GameTimers[3].limit = 0;
        g_GameTimers[3].flags |= 2;
    } else {
        *dst = 0;
    }
    {
        int *flags = (int *)&g_GameTimers[3].flags;

        *flags = (*flags | 1) & ~4;
    }
    return 1;
}

int Script_GetCombatModeFlag(int **arg0) {
    if (g_GameTimers[3].flags & 4) {
        *arg0[0] = 1;
    } else {
        *arg0[0] = 0;
    }

    return 1;
}

int Script_SetCombatModeFlag(int **unused) {
    (void)unused;
    int *ptr = (int *)&g_GameTimers[3].flags;

    *ptr |= 4;
    return 1;
}

int Script_GetEntityEffect(int **arg0) {
    int value;

    value = Scene_LoadRoomAssets(*arg0[0], g_CurrentEntity);
    *arg0[1] = value;
    return 1;
}

int Script_PlayerPointInPoly(int **arg0) {
    PolygonVertex values[4];
    unsigned int i;

    for (i = 0; i < 4; i++) {
        values[i].xFraction = (u16)*arg0[i * 2];
        values[i].x = (s16)((u32)*arg0[i * 2] >> 16);
        values[i].zFraction = (u16)*arg0[i * 2 + 1];
        values[i].z = (s16)((u32)*arg0[i * 2 + 1] >> 16);
    }

    *arg0[8] = Geo_PointInPoly(
        g_CurrentEntity->pos_x,
        g_CurrentEntity->pos_z,
        values,
        4);
    return 1;
}

int Script_InitEntityMoveState(int **unused) {
    (void)unused;
    Entity_AllocSlot((BattleEntity *)g_CurrentEntity);
    return 1;
}

/* The power of an actor's attack, as its script sets it (a mod may change it). */
static int Script_EnemyPower(int power) {
#ifdef PE_MODS
    PeHookEnemyPower hook = { g_CurrentEntity, power };
    PE_HOOK(PE_HOOK_ENEMY_POWER, &hook);
    power = hook.power;
#endif
    return power;
}

int Script_SetEntityAnim12Args(int **arg0) {
    Script_SetObjAnimEntry12(
        (ScriptAnimObj *)g_CurrentEntity,
        (u8)*arg0[0],
        (u8)*arg0[1],
        (u8)*arg0[2],
        (u8)*arg0[3],
        (u16)Script_EnemyPower(*arg0[4]),
        (s8)*arg0[5],
        (s8)*arg0[6],
        (s8)*arg0[7],
        (s8)*arg0[8],
        (u8)*arg0[9],
        (u8)*arg0[10]);
    return 1;
}

int Script_SetEntityAnim5Args(int **arg0) {
    Script_SetObjAnimEntry5(
        (ScriptAnimObj *)g_CurrentEntity,
        (u8)*arg0[0],
        (u8)*arg0[1],
        (u8)*arg0[2],
        (u8)*arg0[3],
        (u16)Script_EnemyPower(*arg0[4]));
    return 1;
}

int Script_SetSceneEntryAnim(int **arg0) {
    int index = *arg0[0];
    GeomEntry *entries = (GeomEntry *)((u8 *)g_GeomState + g_GeomState->entry_offset);

    if ((u32)index >= g_GeomState->entry_count06) {
        /* M052 also addresses record 31 beyond its 20 meshes, within the
         * original geometry bytes. Preserve that byte-level command rather
         * than indexing past the expanded native mesh allocation. */
        s64 offset = (s64)g_GameState.geometry_disk_entry_offset + (s64)index * 0x38;
        GeomEntry record = {0};
        u8 *bytes;
        if (offset < 0 || (u64)offset + 0x30 > g_GameState.geometry_disk_bytes)
            return -1;
        bytes = (u8 *)g_GeomState + offset;
        memcpy(&record, bytes, 0x30);
        Render_TransformPoint(&record, (s16)*arg0[1], (s16)*arg0[2], (s16)*arg0[3]);
        memcpy(bytes, &record, 0x30);
        return 1;
    }
    Render_TransformPoint(
        entries + index,
        (s16)*arg0[1],
        (s16)*arg0[2],
        (s16)*arg0[3]);
    return 1;
}

int Script_SetRenderEntryEnabled(int **arg0) {
    Render_SetEntryVisible(*arg0[0], *arg0[1]);
    return 1;
}

int Script_SetRenderEntryPos(int **arg0) {
    Render_ClipPoint((s16)*arg0[0], (s16)*arg0[1], (s16)*arg0[2]);
    return 1;
}

int Script_SetObjEntryLimit(int **arg0) {
    Render_SetEntryLimit(*arg0[0], *arg0[1]);
    return 1;
}

int Script_SetFlagSlot(int **arg0) {
    Render_SetFlagSlot(*arg0[0], *arg0[1]);
    return 1;
}

int Entity_ClearCurrentFlag20(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= -0x21;
    return 1;
}

int Entity_SetCurrentFlag20(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x20;
    return 1;
}

int Script_GetObjEntryField6(int **arg0) {
    *arg0[1] = Render_GetEntryField6(*arg0[0]);
    return 1;
}

int Script_SetObjEntryField8(int **arg0) {
    Render_SetEntryField8(*arg0[0], *arg0[1]);
    return 1;
}

int Script_FillObjEntrySlotValues(int **arg0) {
    Render_FillEntrySlotValues(*arg0[0], *arg0[1]);
    return 1;
}

int Script_SetObjEntrySlotValue(int **arg0) {
    Render_SetEntrySlotValue(*arg0[0], *arg0[1], *arg0[2]);
    return 1;
}

int Script_SetObjEntryFlags(int **arg0) {
    Render_SetEntryFlags(*arg0[0], *arg0[1]);
    return 1;
}

int Script_ClearObjEntryFlags(int **arg0) {
    Render_SetEntryFlags(*arg0[0], ~*arg0[1]);
    return 1;
}

/* Script ops: set or clear the "not walkable from a neighbour" bit (0x80)
 * of the collision triangle args[0], in whichever of the flat (XZ) and
 * sloped (XYZ) tables the room uses. The triangle address is an integer
 * offset added to the table address; each branch updates its triangle. */
int Script_SetObjEntryFlag80(int **arg0) {
    if (g_CollisionPlaneTable == 0) {
        CollisionTriangleTable entry;

        entry.word = *arg0[0] * sizeof(CollisionTriangleXZ) + g_CollisionDb->triangles.word;
        entry.header->kind |= 0x80;
    } else {
        CollisionTriangleTable entry;

        entry.word = *arg0[0] * sizeof(CollisionTriangleXYZ) + g_CollisionDb->triangles.word;
        entry.header->kind |= 0x80;
    }
    return 1;
}

int Script_ClearObjEntryFlag80(int **arg0) {
    if (g_CollisionPlaneTable == 0) {
        CollisionTriangleTable entry;

        entry.word = *arg0[0] * sizeof(CollisionTriangleXZ) + g_CollisionDb->triangles.word;
        entry.header->kind &= 0x7F;
    } else {
        CollisionTriangleTable entry;

        entry.word = *arg0[0] * sizeof(CollisionTriangleXYZ) + g_CollisionDb->triangles.word;
        entry.header->kind &= 0x7F;
    }
    return 1;
}

int Script_LoadGeomState(int **arg0) {
    Render_LoadGeomState(**arg0);
    return 1;
}

int Script_SetScreenScrollPos(int **arg0) {
    ((CameraBaseAngles *)&g_GeomScroll.state.position.baseAngleDigital)->x = *arg0[0];
    ((CameraBaseAngles *)&g_GeomScroll.state.position.baseAngleDigital)->y = *arg0[1];
    return 1;
}

int Script_SetCdRomSeekPos(int **arg0) {
    CdRom_SetSeekPos((u16)**arg0);
    return 1;
}

int Script_SetFogColor(int **arg0) {
    Render_SetFadeColour((u16)**arg0);
    return 1;
}

int Script_SetCdRomScreenPos(int **arg0) {
    return CdRom_SetScreenPos((u16)*arg0[0], (u16)*arg0[1], (u16)*arg0[2], (u16)*arg0[3], (u16)*arg0[4]), 1;
}

int Script_ClearEntityFlag40(int **unused) {
    (void)unused;
    unsigned char *ptr = &g_ScreenTint.fadeMode;

    *ptr &= 0xBF;
    return 1;
}

int Script_SetEntityMoveSpeed(int **arg0) {
    int **args = arg0;
    FieldActor *current = g_CurrentEntity;
    FieldActor *selected = g_PlayerEntity;
    int value = *args[0] >> 4;

    current->move_speed = value;
    if (current == selected) {
        int scaled;

        scaled = (s32)(((u32)(*args[0] >> 4) * 3u) << 7);

        if (scaled < 0) {
            scaled += 0xFFF;
        }
        g_GeomScroll.state.position.focusHeight = scaled >> 12;
    }
    return 1;
}

short Seq_GetElapsed(void);

int Script_WaitFrameOrDraw(int **arg0) {
    if (Seq_GetElapsed() == *arg0[0]) {
        return 1;
    }
    if ((u8)Draw_CheckDrawStatus() != 0) {
        g_ScriptCursorAddress -= 0xC;
        g_ScriptNodePool->active = 1;
    } else {
        return 1;
    }

    return 0;
}

int Script_Noop1(int **unused) {
    (void)unused;
    return 1;
}

int Script_Noop2(int **unused) {
    (void)unused;
    return 1;
}

int Entity_YieldOnDrawBusy(int **unused) {
    (void)unused;
    if ((u8)Draw_CheckDrawStatus() != 0) {
        g_ScriptCursorAddress -= 8;
        g_ScriptNodePool->active = 1;
    } else {
        return 1;
    }

    return 0;
}

int Script_Noop3(int **unused) {
    (void)unused;
    return 1;
}

int Script_Noop4(int **unused) {
    (void)unused;
    return 1;
}

int Script_SetAnimInterpRate(int **arg0) {
    int **args = arg0;
    FieldActor *current;
    FieldActor *selected;
    unsigned short flags;

    Anim_SetInterpRate(&g_CurrentEntity->render_object, (s16)*args[0]);

    current = g_CurrentEntity;
    flags = current->render_object.flags_9C;
    selected = g_PlayerEntity;
    current->render_object.flags_9C = flags | 2;

    if (current == selected) {
        Anim_SetInterpRate(&g_PlayerBattleEntity, (s16)*args[0]);
        g_PlayerBattleEntity.flags_9C |= 2;
    }

    return 1;
}

int Menu_GetModeState(int **arg0) {
    *arg0[0] = g_BattleModeState;
    return 1;
}

/* Attachment animation and control use the same current actor. */

int Entity_CopyAttachmentData(int **arg0)
{
    int **args = arg0;
    FieldActor *selected;
    RenderObjectEntity *src;
    if (*args[0] == 0)
    {
        FieldActor *current = g_PlayerEntity;
        if (current == 0)
        {
            return 1;
        }
        selected = current;
    }
    else
    {
        int id = *args[0];
        selected = g_FieldActorListHead;
        if (selected == 0)
        {
            return 1;
        }
        while (selected != 0)
        {
            if (((selected->type_id == id) && (selected->sub_id == (*args[1]))) && ((selected->flags & 0x10) == 0))
            {
                break;
            }
            selected = selected->next;
        }

        if (selected == 0)
        {
            return 1;
        }
    }
    src = &selected->render_object;
    Render_SetObjectAnim(&g_CurrentEntity->render_object, src, *(short *)args[2]);
    g_CurrentEntity->parent = selected;
    return 1;
}

/* Adjacent script handlers share actor selection and render-object state. */

int Script_ResetEntityRenderObj(int **unused) {
    (void)unused;
    Render_ClearObjectAnim(&g_CurrentEntity->render_object);
    g_CurrentEntity->parent = 0;
    return 1;
}

int Script_SetEntityField1E6(int **arg0) {
    g_CurrentEntity->render_object.table_index = *arg0[0];
    return 1;
}

int Menu_ClearModeState(int **unused) {
    (void)unused;
    g_BattleModeState = 0;
    return 1;
}

int Menu_SetModeState8(int **unused) {
    (void)unused;
    g_BattleModeState = 8;
    return 1;
}

int Menu_SetSlotEntry(int index, unsigned int limit, unsigned int slot);

int Pm_ScriptStopCurrentEntity(int **arg0) {
    Pm_Stop(*arg0[0], g_CurrentEntity, 0);
    return 1;
}

int Script_SetEntityTarget(int **arg0) {
    int **args = arg0;
    FieldActor *selected;

    if (*args[0] == 0) {
        selected = g_PlayerEntity;
    } else {
        int id = *args[0];

        selected = g_FieldActorListHead;
        while (selected != 0) {
            if (selected->type_id == id &&
                    selected->sub_id == *args[1] &&
                    ((selected->flags & 0x10) == 0)) {
                break;
            }
            selected = selected->next;
        }
    }

    Scene_FreeEntityTable(selected);
    return 1;
}

int Script_ResetRenderState(int **unused) {
    (void)unused;
    Pm_StopAllBoth();
    return 1;
}

int Script_SetMenuSlotEntry(int **arg0) {
    Menu_SetSlotEntry(*arg0[0], *arg0[1], *arg0[2]);
    return 1;
}

int Entity_YieldOnLoading(int **unused) {
    (void)unused;
    if ((g_ScreenTint.fadeMode & 3) >= 2) {
        g_ScriptCursorAddress -= 8;
        g_ScriptNodePool->active = 1;
        return 0;
    }

    return 1;
}

int Script_ScaleAnimValue(int **arg0) {
    int value = *arg0[0];
    int scale = g_CurrentEntity->render_object.hit_cylinder.radius * 2;
    RenderObjectHeader *dst = g_CurrentEntity->render_object.header;

    dst->shadow_radius = (s32)((u32)scale * (u32)value) >> 16;
    return 1;
}

int Script_ClampMenuRange(int **arg0) {
    Menu_ClampRange(*arg0[0]);
    return 1;
}

int Script_GiveItem(int **arg0) {
    *arg0[1] = Inv_AddItem(*arg0[0]);
    return 1;
}

int Script_CountItemsByValue(int **arg0) {
    if (!PE_HOOK(PE_HOOK_ITEM_COUNT, arg0)) *arg0[1] = Inv_CountByValue(*arg0[0]);
    return 1;
}

s32 Menu_OpenEquipSlotSwap(s32 arg0);

int Entity_InitWithTimeout(int **arg0) {
    ScriptNode *state = g_ScriptNodePool;
    unsigned short flags = state->flags;

    if ((flags & 0x20) == 0) {
        state->flags = flags | 0x20;
        Render_BeginSceneLoad();
        g_ScriptCursorAddress -= 0x10;
        g_ScriptNodePool->active = 1;
        return 0;
    }

    if ((unsigned short)(g_MenuFrameCommand - 3) < 0x180) {
        *arg0[1] = g_MenuFrameCommand - 3;
        g_ScriptNodePool->flags &= 0xFFDF;
        return 1;
    }

    if (g_MenuFrameCommand == -1) {
        *arg0[1] = -1;
        g_ScriptNodePool->flags &= 0xFFDF;
        return 1;
    }

    Menu_OpenEquipSlotSwap(*arg0[0]);
    g_ScriptCursorAddress -= 0x10;
    g_ScriptNodePool->active = 1;
    return 0;
}

int Script_SetSceneFlag2000(int **unused) {
    u32 *ptr = &g_GameState.flags;

    *ptr |= 0x2000;
    return 1;
}

int Script_ClearSceneFlag2000(int **unused) {
    u32 *ptr = &g_GameState.flags;

    *ptr &= -0x2001;
    return 1;
}

int Entity_SetCurrentFlag80(int **unused) {
    g_CurrentEntity->flags |= 0x80;
    return 1;
}

int Entity_ClearCurrentFlag80(int **unused) {
    g_CurrentEntity->flags &= -0x81;
    return 1;
}

int Entity_SetCurrentFlag4000(int **unused) {
    g_CurrentEntity->flags |= 0x4000;
    return 1;
}

int Entity_ClearCurrentFlag4000(int **unused) {
    g_CurrentEntity->flags &= -0x4001;
    return 1;
}

int Script_SetAnimSlotPointer(int **arg0) {
    int index = *arg0[0];

    g_EntityCollisionWallSlot = (PolygonVertex *)(g_CurrentEntity->script_base + (ptrdiff_t)index * 2);
    g_EntityCollisionWallParam = *arg0[1];
    return 1;
}

int Script_SetInputFlagBit2(int **unused) {
    g_FieldMoveLock |= 4;
    return 1;
}

int Script_ClearInputFlagBit2(int **unused) {
    g_FieldMoveLock &= -5;
    return 1;
}

int Entity_ResolveCurrentPosition(int **arg0) {
    Entity_ResolvePosition((BattleEntity *)g_CurrentEntity, (u16)*arg0[0]);
    return 1;
}

int Script_GetFloorNumber(int **arg0) {
    *arg0[0] = (unsigned char)Menu_GetEquipSlotStateOrIndex();
    return 1;
}

int Script_SetMoveFlag(int **unused) {
    Script_EnableMovement();
    return 1;
}

int Script_ClearMoveFlag(int **unused) {
    Script_DisableMovement();
    return 1;
}

int Entity_PolarToPosition2(int **arg0)
{
  int **args;
  FieldActor *new_var;
  int radius;
  FieldActor *current;
  FieldActor *current_v1;
  int *src;
  int *dst;
  int value;
  new_var = g_CurrentEntity;
  current = new_var;
  args = arg0;
  src = args[0];
  radius = *src;
  value = rsin(current->rot_y);
  radius = (s32)(0u - (u32)radius);
  value = Math_FixedMul(radius, (s32)((u32)value << 4));
  current_v1 = g_CurrentEntity;
  dst = args[1];
  *dst = (s32)((u32)g_CurrentEntity->pos_x + (u32)value);
  current = g_CurrentEntity;
  value = rcos(current->rot_y);
  value = Math_FixedMul(radius, (s32)((u32)value << 4));
  current_v1 = g_CurrentEntity;
  dst = args[2];
  *args[2] = (s32)((u32)current_v1->pos_z + (u32)value);
  return 1;
}

/* Contiguous animation-entry commands share the current actor. */


int Script_SetBattleEntryCoords(int **arg0) {
    Battle_SetEntryCoords((ScriptAnimObj *)g_CurrentEntity, *(unsigned char *)arg0[0], *arg0[1], *arg0[2]);
    return 1;
}

int Script_ClearEntityFlag1(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= -2;
    return 1;
}

int Script_SetEntityFlag1(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 1;
    return 1;
}

int Script_SetEntryScrolled(int **arg0) {
    Render_SetEntryScrolled(*arg0[0], *arg0[1], *arg0[2], *arg0[3]);
    return 1;
}

int Script_SetEntryMirrored(int **arg0) {
    Render_SetEntryMirrored(*arg0[0], *arg0[1], *arg0[2], *arg0[3]);
    return 1;
}

int Script_SetRenderFlag10Value(int **arg0) {
    g_CurrentEntity->render_object.script_value9a = *arg0[0];
    g_CurrentEntity->render_object.flags_9C |= 0x10;
    return 1;
}

int Script_ClearRenderFlag10(int **unused) {
    (void)unused;
    g_CurrentEntity->render_object.flags_9C &= 0xFFEF;
    return 1;
}

int Script_SetRenderFlag8Params(int **arg0) {
    g_CurrentEntity->render_object.script_value9a = *arg0[0];
    g_CurrentEntity->render_object.script_param97 = *arg0[1];
    g_CurrentEntity->render_object.script_param98 = *arg0[2];
    g_CurrentEntity->render_object.script_param99 = *arg0[3];
    g_CurrentEntity->render_object.flags_9C |= 8;
    return 1;
}

int Script_ClearRenderFlag8(int **unused) {
    (void)unused;
    g_CurrentEntity->render_object.flags_9C &= 0xFFF7;
    return 1;
}

int Script_SetEntityFlag400(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x400;
    return 1;
}

int Script_ClearEntityFlag400(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= -0x401;
    return 1;
}

int Script_WaitEntityFlags8(int **unused) {
    (void)unused;
    ScriptNode *state;
    unsigned short flags;

    state = g_ScriptNodePool;
    flags = state->flags;

    if (flags & 0x20) {
        if (g_CurrentEntity->flags & 8) {
            state->flags = flags & 0xFFDF;
            return 1;
        }
    } else {
        state->flags = flags | 0x20;
    }

    g_ScriptCursorAddress -= 8;
    g_ScriptNodePool->active = 1;
    return 0;
}

int Script_RotateEntityAngle(int **arg0) {
    int *src;
    FieldActor *actor;
    int value;
    int delta;
    char frame[8];
    src = arg0[0];
    actor = g_CurrentEntity;
    delta = src[0];
    value = (u16)actor->rot_y + delta;
    actor->rot_y = value;
    if ((short)value >= 0x1001) {
        int adjusted;
        adjusted = value - 0x1000;
        actor->rot_y = adjusted;
    }
    actor = g_CurrentEntity;
    {
        int signed_value;
        signed_value = actor->rot_y;
        value = signed_value;
        if (signed_value < 0) {
            signed_value = 0x1000;
            signed_value = value + signed_value;
            actor->rot_y = signed_value;
        }
    }
    return 1;
}

int Script_SetNodeFlag80(int **unused) {
    g_ScriptNodePool->flags |= 0x80;
    return 1;
}

/* Script opcodes from 0x80019C04 up to 0x8001A4AC: roam flag, angle to a point,
 * spawn snap, scene exit and save overlay, game-time conversion, actor parent
 * links, relative position, script-base queries and inventory helpers. All are default-profile handlers that
 * read the current actor through absolute addresses. */


int Script_ClearEntityRoamFlag(int **unused) {
    g_CurrentEntity->flags &= 0xFFFDFFFF;
    return 1;
}

int Script_SetEntityRoamFlag(int **unused) {
    g_CurrentEntity->flags |= 0x20000;
    return 1;
}

int Script_GetAngleToPoint(int **arg0) {
    int angle;
    int dx;
    int dy;

    dx = (g_CurrentEntity->pos_x - *arg0[0]) >> 16;
    dy = (g_CurrentEntity->pos_z - *arg0[1]) >> 16;
    angle = 0x1400 - ratan2(dy, dx);
    if (angle >= 0x1001) {
        angle -= 0x1000;
    }
    angle -= g_CurrentEntity->rot_y;
    if (angle < 0) {
        angle += 0x1000;
    }
    *arg0[2] = angle;
    return 1;
}

int Script_SnapPosToSpawnPos(int **unused) {
    FieldActor *actor = g_CurrentEntity;

    actor->pos_x = (s32)((u32)actor->render_object.hit_cylinder.value0 << 16);
    actor->pos_y = (s32)((u32)actor->render_object.hit_cylinder.value1 << 16);
    actor->pos_z = (s32)((u32)actor->render_object.hit_cylinder.value2 << 16);
    return 1;
}

int Script_SignalSceneExit(int **unused) {
    (void)unused;
    Script_SignalExit();
    return 1;
}

int Script_DrawSaveOverlay(int **arg0) {
    Menu_SetTextCursorRect((u16)*arg0[0], (u16)*arg0[1], (u16)*arg0[2], (u16)*arg0[3]);
    return 1;
}

int Script_ResetEntityAnimSlot(int **arg0) {
    short local[8];

    local[0] = -1;
    Render_SetupColorTable((s16)*arg0[0], 1, local);
    return 1;
}

int Script_GetFloorTableValue(int **arg0) {
    *arg0[1] = GAME_TIME_COUNTER(*arg0[0]);
    return 1;
}

s32 GameTime_ConvertSecondsToHMS(int **arg0) {
    *arg0[1] = *arg0[0] / 216000;
    *arg0[2] = (*arg0[0] % 216000) / 3600;
    *arg0[3] = (*arg0[0] % 3600) / 60;
    return 1;
}

/* Script op: attach the current entity to the actor named by
 * (args[0], args[1]) as its parent (pos/rot follow when flags & 0x400000). */
s32 Script_SetEntityParentLink(s32 *args[]) {
    s32 key;
    FieldActor *node;

    key = *args[0];
    if (key == 0) {
        FieldActor *tmp;

        tmp = g_PlayerEntity;
        if (tmp == NULL) {
            return 1;
        }
        node = tmp;
        {
            FieldActor *cur = g_CurrentEntity;
            cur->parent = node;
            node->flags |= 0x100000;
            cur->flags |= 0x600000;
        }
        return 1;
    }
    {
        u32 k = key;
        node = g_FieldActorListHead;
        if (node != NULL) {
            do {
                if ((node->type_id != k) || (node->sub_id != *args[1]) || (node->flags & 0x10)) {
                    node = node->next;
                } else {
                    break;
                }
            } while (node != NULL);
            if (node == NULL) {
                return 1;
            }
        } else {
            return 1;
        }
    }
    {
        FieldActor *cur = g_CurrentEntity;
        cur->parent = node;
        node->flags |= 0x100000;
        cur->flags |= 0x600000;
    }
    return 1;
}

int Script_ClearEntityParentLink(int **unused) {
    FieldActor *current;
    FieldActor *it;

    current = g_CurrentEntity;
    it = g_FieldActorListHead;
    current->parent = 0;
    current->flags &= 0xFF9FFFFF;

    if (it != 0) {
        do {
            if (it != current) {
                if (it->parent == current->parent) {
                    return 1;
                }
            }
            it = it->next;
        } while (it != 0);
    }

    {
        FieldActor *tail_current;
        int clear_mask;
        tail_current = g_CurrentEntity;
        clear_mask = 0xFFEFFFFF;
        /* Retail reaches PS1 low RAM after clearing parent. A NULL parent
         * is not a native actor whose flags can be updated. */
        if (tail_current->parent != NULL)
            tail_current->parent->flags &= clear_mask;
    }
    return 1;
}

int Entity_ClearParentLink(int **unused) {
    int clear_current = 0xFFEFFFFF;
    FieldActor *current = g_CurrentEntity;
    FieldActor *it = g_FieldActorListHead;

    current->flags &= clear_current;

    if (it != 0) {
        int clear_child = 0xFF9FFFFF;

        do {
        if (it->parent == current) {
            it->parent = 0;
                it->flags &= clear_child;
        }
            it = it->next;
        } while (it != 0);
    }

    return 1;
}

int Script_GetSin(int **arg0) {
    *arg0[1] = (s32)((u32)rsin(*arg0[0]) << 4);
    return 1;
}

int Script_GetCos(int **arg0) {
    *arg0[1] = (s32)((u32)rcos(*arg0[0]) << 4);
    return 1;
}

int Script_GetAtan2(int **arg0) {
    *arg0[2] = ratan2(*arg0[0], *arg0[1]);
    return 1;
}

int Script_GetSqrt(int **arg0) {
    *arg0[1] = Math_IntSqrt(*arg0[0]);
    return 1;
}

int Script_SetEntityFlag1000000(int **unused) {
    g_CurrentEntity->flags |= 0x1000000;
    return 1;
}

int Entity_PolarToPosition(int **arg0)
{
    int **args;
    short angle;
    int radius;
    int *ptr;
    int value;
    int radiusInput;
    int *angleInput;
    FieldActor *current_v1;
    int angleValue;
    FieldActor *current_v0;
    int base;
    args = arg0;
    angleInput = args[1];
    ptr = angleInput;
    angleValue = *ptr;
    value = angleValue;
    angle = (s16)(0x1400u - (u32)value);
    ptr = args[0];
    radiusInput = *ptr;
    radius = radiusInput;
    value = rcos(angle);
    radius = (s32)(0u - (u32)radius);
    value = Math_FixedMul(radius, (s32)((u32)value << 4));
    {
        int *dst;
        dst = args[2];
        *dst = value;
    }
    value = rsin(angle);
    value = Math_FixedMul(radius, (s32)((u32)value << 4));
    {
        int *dst;
        dst = args[3];
        *dst = value;
    }
    {
        int *dst;
        dst = args[2];
        current_v1 = g_CurrentEntity;
        value = *dst;
        value = (s32)((u32)value + (u32)current_v1->pos_x);
        *dst = value;
    }
    {
        int *dst;
        int loaded;
        dst = args[3];
        current_v0 = g_CurrentEntity;
        loaded = *dst;
        base = current_v0->pos_z;
        loaded = (s32)((u32)loaded + (u32)base);
        *dst = loaded;
    }
    return 1;
}

int Entity_PopCount(int **arg0) {
    u32 value;
    int count;

    value = *arg0[0];
    count = 0;
    while (value != 0) {
        value &= value - 1;
        count++;
    }

    *arg0[1] = count;
    return 1;
}

int Script_EnableEntityGravity(int **unused) {
    g_CurrentEntity->flags |= 2;
    return 1;
}

int Script_DisableEntityGravity(int **unused) {
    g_CurrentEntity->flags &= -3;
    return 1;
}

int Script_SetMeshFadeLevel(int **arg0) {
    g_GeomScroll.state.position.meshTintLevel = *arg0[0];
    return 1;
}

int Entity_CallAction(int **arg0)
{
    int **args;
    int *offsetInput;
    int **xSlot;
    int polygonOffset;
    u8 *base;
    FieldActor *current;
    args = arg0;
    offsetInput = args[3];
    polygonOffset = *offsetInput;
    current = g_CurrentEntity;
    xSlot = &args[0];
    base = current->script_base;
    polygonOffset = (s32)((u32)polygonOffset << 1);
    *args[4] = Geo_PointInPoly(**xSlot, *args[1], (const PolygonVertex *) (base + polygonOffset), *(u16 *)args[2]);
    return 1;
}

int Script_SetInventorySlotPointer(int **arg0) {
    g_FloorPolygonVertices = (PolygonVertex *)(g_CurrentEntity->script_base + (ptrdiff_t)*arg0[0] * 2);
    g_FloorPolygonVertexCount = *arg0[1];
    return 1;
}

int Script_GetInventoryTotal(int **arg0) {
    *arg0[0] = Inv_CountTotal();
    return 1;
}

int Script_GetInventorySlotLimit(int **arg0) {
    *arg0[0] = Inv_GetAyaSlotLimit();
    return 1;
}

int Pm_ScriptSendCommand0(int **args) {
    Pm_SendCmd(*args[0], 0, *args[1], *args[2], *args[3], *args[4]);
    return 1;
}

int Pm_ScriptSendCommand1(int **arg0) {
    int **base;
    int *op0;
    int *op1;
    int *stack0;
    int first;
    int second;

    base = arg0;
    op0 = base[0];
    op1 = base[1];
    stack0 = base[3];
    first = *op0;
    second = *op1;

    Pm_SendCmd(first, 1, second, (intptr_t)base[2], (intptr_t)stack0, (intptr_t)base[4]);
    return 1;
}

int Pm_ScriptSetState(int **arg0) {
    Pm_SetGetState(*arg0[0], 0, *arg0[1]);
    return 1;
}

int Pm_ScriptGetState(int **arg0) {
    Pm_SetGetState(*arg0[0], 1, (intptr_t)arg0[1]);
    return 1;
}
