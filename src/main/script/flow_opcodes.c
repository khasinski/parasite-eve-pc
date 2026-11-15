#include "common.h"
#include "pe1/pe_image.h"
#include "pe_hooks.h"
#include "pe1/field_sprite_state.h"
#include "pe1/akao_send.h"
#include <stdio.h>
#include <stdlib.h>
#include "pe1/script_expression.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_update.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_animation.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/field_sfx.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/geom_state.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_state.h"
#include "pe1/player_entity.h"
#include "pe1/render_lighting.h"
#include "pe1/render_shadow.h"
#include "pe1/scene_room_load.h"
#include "pe1/scene_transition.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_node.h"
#include "pe1/script_queue.h"
#include "pe1/script_state.h"
#include "pe1/textbox.h"
#include "pe1/vector_types.h"

/* Shared native state; external variables have one storage definition. */
u8 g_ScriptCameraBytes[3];

int Script_DispatchScriptCmd(int **args)
{
    Pe1Vec3s position;
    Pe1Vec3s position2;
    char map_id[2];
    FieldActor *entity;
    FieldActor *loaded;
    int status;
    int entity_id;
    int initial_entity_id;

    /* Opcode 2100 is accepted and does nothing; its case node is what
     * makes the compiler split the dispatch tree at 2500 as retail does. */
    switch (*args[0]) {
    case 2100:
        break;
    case 2101:
        g_GameState.flags |= 0x800;
        break;
    case 2200:
        Render_UpdateScrollPosition(&(g_PlayerEntity)->pos_x,
                                    *args[1], *args[2]);
        return 1;
    case 2201:
        g_GeomScroll.state.position.originX = *args[1];
        g_GeomScroll.state.position.originY = *args[2];
        break;
    case 2300:
        Script_SetCollisionFlag(*args[1] != 0);
        return 1;
    case 2400:
        position.x = (s16)((u32)*args[1] >> 16);
        position.y = (s16)((u32)*args[2] >> 16);
        position.z = (s16)((u32)*args[3] >> 16);
        FieldFx_SpawnScriptSprite(&position, 0, (s16)*args[4],
                      (u8)*args[5], 0, 0, 0);
        return 1;
    case 2401:
        g_ScriptCameraBytes[0] = *args[1];
        g_ScriptCameraBytes[1] = *args[2];
        g_ScriptCameraBytes[2] = *args[3];
        break;
    case 2402:
        position2.x = (s16)((u32)*args[1] >> 16);
        position2.y = (s16)((u32)*args[2] >> 16);
        position2.z = (s16)((u32)*args[3] >> 16);
        FieldFx_SpawnScriptSprite(&position2, 1, (s16)*args[4],
                      (u8)*args[5], g_ScriptCameraBytes[0],
                      g_ScriptCameraBytes[1], g_ScriptCameraBytes[2]);
        return 1;
    case 2500:
        map_id[0] = *args[1];
        map_id[1] = *args[2];
        loaded = Scene_LoadMap((u8 *)map_id, g_CurrentEntity, 0);
        if (!loaded) return -1;
        loaded->pos_x = *args[3];
        loaded->pos_y = *args[4];
        loaded->pos_z = *args[5];
        break;
    case 2600:
        ((RenderShadowActor *)g_CurrentEntity)->shadow_matrix_index = *args[1];
        break;
    case 2601:
        ((RenderShadowActor *)g_CurrentEntity)->shadow_intensity = *args[1];
        break;
    case 2700:
        g_RoomLightTint.r = *args[1];
        g_RoomLightTint.g = *args[2];
        g_RoomLightTint.b = *args[3];
        break;
    case 2800:
        Battle_DrawActiveStatus();
        return 1;
    case 2801:
        Battle_SetWindowBoundsByMode((u8)*args[1]);
        return 1;
    case 2900:
        g_GameState.flags |= 0x400000;
        break;
    case 3000:
        initial_entity_id = *args[1];
        if (initial_entity_id == 0) {
            return 1;
        }
        entity = g_FieldActorListHead;
        entity_id = initial_entity_id;
        if (entity == 0) {
            return 1;
        }
        while (1) {
            if (entity->type_id == entity_id) {
                if (entity->sub_id == *args[2]) {
                    if ((entity->flags & 0x10) == 0) {
                        return 1;
                    }
                }
            }
            entity = entity->next;
            if (entity == 0) {
                break;
            }
        }
        return 1;
    case 3100:
        status = Asset_LoadTimTextures(1);
        if (status == 1) {
            g_ScriptCursor -= 10;
            g_ScriptNodePool->active = status;
            return 0;
        }
        return 1;
    case 3200:
        g_GameState.flags |= 0x8000000;
        break;
    }
    return 1;
}

int Script_SetEntityShadowFlag(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x20000000;
    return 1;
}

int Script_ClearEntityShadowFlag(int **unused) {
    (void)unused;
    g_CurrentEntity->flags &= 0xDFFFFFFF;
    return 1;
}

int Script_BeginSceneTransition(int **arg0) {
    Pe1GameState *state = &g_GameState;
    int **saved = arg0;
    ScriptNode *node;
    unsigned short *node_flags;
    int flags;
    uintptr_t cursor;

    if (!(state->flags & 0x1000)) {

    node = g_ScriptNodePool;
    node_flags = &node->flags;
    flags = *node_flags;
    if ((flags & 0x20) == 0) {
        *node_flags = flags | 0x20;
        cursor = g_ScriptCursorAddress;
        node->active = 1;
        cursor -= 0xC;
        g_ScriptCursorAddress = cursor;
        return 0;
    }

    Render_BeginSceneLoad();
    Menu_OpenEquipScreen(**saved);
    {
        ScriptNode *tail_node = g_ScriptNodePool;
        int tail_flags;

        state->flags |= 0x9000;
        tail_flags = tail_node->flags;
        g_GameStateFlags |= 4;
        tail_flags &= 0xFFDF;
        tail_node->flags = tail_flags;
    }

    }
ret_one:
    return 1;
}

void Menu_OpenSaveLoadEntryPoint(int arg0);

int Script_OpenSaveLoadMenu(int **arg0) {
    Pe1GameState *state = &g_GameState;
    int **saved = arg0;
    ScriptNode *node;
    int flags;
    uintptr_t cursor;

    if (PE_JAPAN) {
        /* The Japanese release: the first pass marks the node and runs the opcode again next
         * frame; the second opens the menu and lets the script go on in the same frame. */
        if (state->flags & 0x1000) return 1;
        node = g_ScriptNodePool;
        if (!(node->flags & 0x20)) {
            node->flags |= 0x20;
            node->active = 1;
            g_ScriptCursorAddress -= 0xC;
            return 0;
        }
        Render_BeginSceneLoad();
        Menu_OpenSaveLoadEntryPoint(**saved);
        state->flags |= 0x9000;
        g_GameStateFlags |= 4;
        g_ScriptNodePool->flags &= 0xFFDF;
        return 1;
    }

    if (!(state->flags & 0x1000)) {

    node = g_ScriptNodePool;
    flags = node->flags;
    if (flags & 0x20) {
        Render_BeginSceneLoad();
        Menu_OpenSaveLoadEntryPoint(**saved);
        {
            ScriptNode *tail_node = g_ScriptNodePool;
            int tail_flags;

            state->flags |= 0x9000;
            tail_flags = tail_node->flags;
            g_GameStateFlags |= 4;
            tail_flags &= 0xFFDF;
            tail_node->flags = tail_flags;
        }
    } else {
        cursor = g_ScriptCursorAddress;
        node->flags = flags | 0x20;
        cursor -= 0xC;
        g_ScriptCursorAddress = cursor;
    }

    {
        ScriptNode *mark_node = g_ScriptNodePool;
        int one = 1;

        mark_node->active = one;
    }

    }
ret_zero:
    return 0;
}

int Script_GetPlayerInputFlag(int **args) {
    if (D_8009D2E8 & 1) {
        *args[0] = 0;
    } else {
        *args[0] = 1;
    }

    return 1;
}

/* Script interpreter main loop and the jump-to-actor-script opcode; a
 * contiguous -G8 pair sharing the script cursor g_ScriptCursor. */

PE1_STATIC_ASSERT(sizeof(((ScriptNode *)0)->current) == sizeof(intptr_t), interpreter_cursor_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(ScriptNode, current) == 0, interpreter_script_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(ScriptNode, active) == PE1_OFFSETOF(ScriptNode, trigger_value) + sizeof(int), interpreter_wait_offset);

/* Shared native state; external variables have one storage definition. */













int Script_SpawnEntityAt(int **arg0) {
    char local[2];
    FieldActor *entity;

    local[0] = arg0[0][0];
    local[1] = arg0[1][0];
    entity = Scene_LoadMap((u8 *)local, g_CurrentEntity, 1);
    if (!entity) return -1;
    entity->pos_x = arg0[2][0];
    entity->pos_y = arg0[3][0];
    entity->pos_z = arg0[4][0];
    Entity_FindFloor((struct BattleEntity *)entity);
    return 1;
}


int Script_StopEntityAnim(int **arg0) {
    s16 value[5];

    value[0] = -1;
    Render_SetupColorTable((s16)*arg0[0], 0, value);
    return 1;
}

int Math_FixedMul(int arg0, int arg1);

int Entity_ComputeVelocityToTarget(int **arg0) {
    FieldActor *state;
    int delta28;
    int delta2C;
    int delta30;
    int scale;
    int temp;

    state = g_CurrentEntity;
    state->flags |= 2;

    delta28 = state->pos_x - *arg0[0];
    delta2C = state->pos_y - *arg0[1];
    delta30 = state->pos_z - *arg0[2];
    scale = (s32)((u32)*arg0[3] << 16);

    g_CurrentEntity->motion_x = Math_FixedDivide(delta28, scale);
    g_CurrentEntity->motion_z = Math_FixedDivide(delta30, scale);
    temp = Math_FixedMul(scale + 0x10000, g_CurrentEntity->gravity_y) >> 1;
    g_CurrentEntity->motion_y = temp;
    g_CurrentEntity->motion_y = Math_FixedDivide(delta2C, scale) + g_CurrentEntity->motion_y;

    return 1;
}

int Entity_YieldWhileMoving(int **unused) {
    int ret;

    ret = 0;
    if ((g_CurrentEntity->flags & 2) == 0) {
        return 1;
    }

    g_ScriptCursorAddress -= 8;
    g_ScriptNodePool->active = 1;
    return ret;
}
int Entity_SelectFieldSetter(int **arg0) {
    int value;
    int selector;

    value = *arg0[1];
    if (value < 0) {
        selector = *arg0[0];
        switch (selector) {
        case 1:
            g_CurrentEntity->script_cursor_1a0 = 0;
            break;
        case 2:
            g_CurrentEntity->script_cursor_19c = 0;
            break;
        case 3:
            g_ScriptNodePool->next_value = 0;
            break;
        }
    } else {
        selector = *arg0[0];
        switch (selector) {
        case 1:
            g_CurrentEntity->script_cursor_1a0 = (unsigned int *)(g_CurrentEntity->script_base + ((size_t)(u32)value * 2));
            break;
        case 2:
            g_CurrentEntity->script_cursor_19c = (unsigned int *)(g_CurrentEntity->script_base + ((size_t)(u32)value * 2));
            break;
        case 3:
            g_ScriptNodePool->next_value = (intptr_t)(g_CurrentEntity->script_base + ((size_t)(u32)value * 2));
            break;
        }
    }

    return 1;
}

int Entity_SetCurrentFlags(int **args)
{
    g_CurrentEntity->flags |= (*args)[0];
    return 1;
}

int Entity_GetCurrentNodeId(int **args)
{
    *args[0] = g_ScriptNodePool->seq;
    return 1;
}

/* Script opcodes: random value in a range and a field sound effect on the
 * current actor. Contiguous default-profile handlers. */

int Draw_PackPrimColor(int start, int end);

/* Equal bounds return one raw RNG word. Otherwise the result is in [start, end). */
int Script_Random(int **arg0) {
    int start = *arg0[1];
    int end = *arg0[2];
    PE_HOOK(PE_HOOK_SCRIPT_RANDOM, (&(PeHookScriptRandom){&start, &end, g_ScriptCursor}));

    if (start == end) {
        *arg0[0] = Draw_FlushPrimQueue();
    } else {
        *arg0[0] = Draw_PackPrimColor(start, end);
    }

    return 1;
}

int Script_PlayFieldSfx(FieldSfxScriptArgs args) {
    int a=(u8)*args[2];
    int b=(u8)*args[1];
    int c=(u16)*args[0];
    int d=g_CurrentEntity->field_sfx_id;
    Script_QueueFieldSfx(a, b, c, d, 0);
    return 1;
}


int Script_GetNodeTarget14(int **arg0) {
    *arg0[0] = g_ScriptNodePool->target14;
    return 1;
}

int Script_YieldWhileTableEntrySet(int **arg0) {
    if ((u8)Draw_GetTextboxPageState((s16)*arg0[0]) != 0) {
        g_ScriptCursorAddress -= 0xC;
        g_ScriptNodePool->active = 1;
        return 0;
    }
    return 1;
}

int Script_ClearTableEntry(int **arg0) {
    Draw_CloseTextboxPage((s16)*arg0[0]);
    return 1;
}

int Script_GetNodeTargets18_1C(int **arg0) {
    *arg0[0] = g_ScriptNodePool->target18.coordinate;
    *arg0[1] = g_ScriptNodePool->target1c;
    return 1;
}

int Entity_FindNodeById(int **arg0) {
    ScriptNode *node;
    ScriptNode *cur;
    unsigned short id;
    unsigned int bucket;
    unsigned int search_id;
    FieldActor *base;

    id = (u16)*arg0[0];
    node = g_ScriptNodePool;
    if (node->seq == id) {
        node->flags |= 0x10;
        return 0;
    }

    base = g_CurrentEntity;
    search_id = id;
    bucket = 0;
    do {
        cur = base->script_node_lists[bucket & 0xFF];
        while (cur != 0) {
            if (cur->seq == search_id) {
                cur->flags |= 0x10;
                return 1;
            }
            cur = cur->next;
        }
        bucket++;
    } while ((bucket & 0xFF) < 3);

    return 1;
}

int Script_GetCurrentEntityFieldD(int **arg0) {
    *arg0[0] = g_CurrentEntity->sub_id;
    return 1;
}

int Script_SetCurrentEntityFieldD(int **arg0) {
    g_CurrentEntity->sub_id = (u16)*arg0[0];
    return 1;
}

int Script_GetCurrentEntityField24(int **arg0) {
    *arg0[0] = g_CurrentEntity->field_sfx_id;
    return 1;
}

int Entity_MarkNodeFree(int **unused) {
    u8 i;
    ScriptNode *skip;
    FieldActor *entity;
    ScriptNode *node;

    i = 0;
    entity = g_CurrentEntity;
    skip = g_ScriptNodePool;
    do {
        node = entity->script_node_lists[i];
        while (node != 0) {
            if (node != skip) {
                node->flags |= 0x10;
            }
            node = node->next;
        }
        i++;
    } while (i < 3);

    return 1;
}

int Script_ClearBit(int **arg0) {
    *arg0[0] = (s32)((u32)*arg0[0] & ~(1u << ((u32)*arg0[1] & 31u)));
    return 1;
}

int Script_TestBit(int **arg0) {
    *arg0[2] = (s32)((u32)*arg0[0] & (1u << ((u32)*arg0[1] & 31u)));
    return 1;
}

int Script_SetBit(int **arg0) {
    *arg0[0] = (s32)((u32)*arg0[0] | (1u << ((u32)*arg0[1] & 31u)));
    return 1;
}

int Entity_ClearGlobalFlags(int **arg0) {
    g_FieldMoveLock &= ~*arg0[0];
    return 1;
}

int Entity_GetGlobalFlags(int **arg0) {
    *arg0[0] = g_FieldMoveLock;
    return 1;
}

int Entity_SetGlobalFlags(int **arg0) {
    g_FieldMoveLock |= *arg0[0];
    return 1;
}

int Entity_SetCurrentActionMode(int **arg0) {
    FieldActor *entity;

    Entity_SetActionMode((BattleEntity *)g_CurrentEntity, (u16)*arg0[0]);
    entity = g_CurrentEntity;
    entity->flags &= ~0x100;
    return 1;
}

int Script_SetEntityAnimFrame(int **arg0) {
    FieldActor *entity = g_CurrentEntity;
    u16 value = (u16)*arg0[0];

    if (entity->action < value) {
        value = entity->action;
    }

    entity->anim_frame_target = value;
    entity->flags |= 0x200;
    return 1;
}

int Script_WaitAnimFrameMatch(int **unused) {
    ScriptNode *state;
    FieldActor *entity;
    uintptr_t stack;
    int ret;

    state = g_ScriptNodePool;
    entity = g_CurrentEntity;
    state->active = 1;
    ret = 0;
    if (entity->anim.parts.integer == entity->anim_frame_target) {
        return ret;
    }

    stack = g_ScriptCursorAddress;
    stack -= 8;
    g_ScriptCursorAddress = stack;
    return ret;
}

ScriptCursorValue g_ScriptCursorValue;

int Script_JumpToEntityOffset(int **arg0)
{
    g_ScriptCursor = (u32 *)(g_CurrentEntity->script_base + (*arg0[0] * 2));
    return 1;
}

int Entity_MarkCurrentDead(int **unused) {
    (void)unused;
    g_CurrentEntity->flags |= 0x10;
    return 0;
}

int Entity_SetCurrentNodeActive(int **arg0) {
    g_ScriptNodePool->active = (u16)*arg0[0];
    return 0;
}

int Entity_MarkCurrentNodeFree(int **unused) {
    (void)unused;
    g_ScriptNodePool->flags |= 0x10;
    return 0;
}

int Script_AssignValue(int **arg0) {
    *arg0[0] = *arg0[1];
    return 1;
}

int Script_JumpIfZero(int **arg0) {
    if (*arg0[0] == 0) {
        g_ScriptCursorAddress = (uintptr_t)(g_CurrentEntity->script_base + (*arg0[1] * 2));
    }
    return 1;
}

void Script_RunQueue(void)
{
    int *args[16];
    ScriptNode *node;
    u8 *entity;
    u32 header, modes, tail, opcode;
    u32 *values;
    FieldActor *argument_entity;
    u16 i;
    int result;

    do {
        node = g_ScriptNodePool;
        /* Retail first reads flags and sequence together as a word. */
        if (node->flags & 0x50) continue;
        entity = (u8 *)g_CurrentEntity;
        if ((((FieldActor *)entity)->flags & 0x1000) && !(node->flags & 0x80)) continue;
        if ((g_GameStateFlags & 0x100) && entity != (u8 *)g_PlayerEntity && !(node->flags & 0x80)) continue;
        node = g_ScriptNodePool;
        if (node->active == 0) continue;
        node->active--;
        if (node->active != 0) continue;
        g_ScriptCursor = node->current.script;
        do {
            header = *g_ScriptCursor;
            g_ScriptCursor++;
            values = g_ScriptCursor + 1;
            tail = *g_ScriptCursor;
            modes = header >> 17;
            opcode = header & 0x1FFF;
            header >>= 13;
            header &= 15;
            g_ScriptCursor = values + header;
            for (i = 0; i < header; i++) {
                switch (modes & 7) {
                case 0: args[i] = (int *)&values[i]; break;
                case 3: args[i] = (int *)&g_ScriptOperandTable[values[i]]; break;
                case 1:
                    /* Reload after every handler: it may switch actors. */
                    argument_entity = g_CurrentEntity;
                    args[i] = (int *)&argument_entity->pad_0AC[values[i]];
                    break;
                case 2: args[i] = (int *)&g_ScriptVariableBank[values[i]]; break;
                case 4: args[i] = &g_ScriptNodeActiveFlags[values[i]]; break;
                }
                modes >>= 3;
                if (i == 4) modes = tail;
            }
            if (!g_ScriptQueueOpcodeHandlers[opcode]) {
                fprintf(stderr, "Unbound original script opcode %u (scene=%08x actor=%u:%u script_byte=%llu)\n",
                    opcode, (u32)g_SceneDispatchToken, g_CurrentEntity->type_id, g_CurrentEntity->sub_id,
                    (unsigned long long)((uintptr_t)g_ScriptCursor-(uintptr_t)g_CurrentEntity->script_base));
                abort();
            }
            result=g_ScriptQueueOpcodeHandlers[opcode](args);
            if(result<0){
                fprintf(stderr,"Original script opcode %u failed: %d\n",opcode,result);
                abort();
            }
        } while (result);
        /* Handlers may replace both the current node and the script cursor. */
        g_ScriptNodePool->current.script = g_ScriptCursor;
    } while ((g_ScriptNodePool = g_ScriptNodePool->next) != 0);
}

/* USA resident dispatcher at 0x800910A0. Unbound entries remain NULL;
 * they must be ported before scenes using them can execute. */
int (*g_ScriptQueueOpcodeHandlers[8192])(int **) = {
    [0]=Script_JumpToEntityOffset, [1]=Entity_MarkCurrentDead,
    [3]=Script_SetFogParams, [70]=Script_SetFogParamsAlt, [2]=Entity_SetCurrentNodeActive, [4]=Entity_MarkNodeFree, [5]=Script_JumpIfZero,
    [8]=Script_SpawnEntityAt, [9]=Script_EvalExpr, [10]=Script_AssignValue,
    [13]=Script_StopEntityAnim, [11]=Script_SetEntityVec3, [12]=Script_GetEntityVec3, [14]=Entity_QueryField,
    [17]=Script_DispatchCmd, [18]=Script_SpawnChildNode, [19]=Entity_SetCurrentFlags, [20]=Entity_SelectFieldSetter,
    [22]=Entity_YieldWhileMoving, [23]=Entity_ComputeVelocityToTarget,
    [24]=Entity_FindNodeById, [25]=Entity_GetCurrentNodeId, [27]=Script_SetEntityMoveSpeed,
    [26]=Script_Random,
    [28]=Script_PlayFieldSfx, [29]=Script_CondBranch,
    [34]=Script_YieldWhileTableEntrySet, [35]=Script_ClearTableEntry,
    [30]=Entity_SetCurrentFlag80, [31]=Script_GetNodeTarget14, [32]=Entity_MarkCurrentNodeFree,
    [36]=Script_GetNodeTargets18_1C, [37]=Script_GetCurrentEntityField24,
    [38]=Script_GetCurrentEntityFieldD, [39]=Script_SetCurrentEntityFieldD,
    [40]=Script_ClearBit,
    [41]=Script_TestBit, [42]=Script_SetBit,
    [43]=Entity_ClearGlobalFlags, [44]=Entity_GetGlobalFlags, [45]=Entity_SetGlobalFlags,
    [49]=Script_EncodeBarcode, [53]=Script_LoadSceneData, [54]=Render_CameraTrackEntityZoom,
    [63]=Entity_ClearGlobalFlag1, [64]=Entity_SetGlobalFlag1,
    [62]=Script_SetEntityField224,
    [65]=Entity_SetCurrentFlag40, [66]=Entity_ClearCurrentFlag40,
    [67]=Script_GetMenuChoice,
    [71]=Script_IsBattlePhase4, [72]=Script_ResetPlayerCameraPos,
    [73]=Script_SetBattleStateMode,
    [103]=Script_ComputeAudioTimer, [104]=Script_GetCombatModeFlag,
    [105]=Script_SetCombatModeFlag, [211]=GameTime_ConvertSecondsToHMS,
    [93]=Script_SetCurrentEntityField20, [77]=Script_SetEntityField1C,
    [202]=Script_SetEntityField1E6, [210]=Script_GetFloorTableValue,
    [21]=Script_PlayerPointInPoly, [218]=Script_GetSqrt, [219]=Render_CameraTrackEntityAngle,
    [164]=Script_CopyEntityRenderObject, [165]=Boot_CheckModeSwitch,
    [167]=Script_GiveItem, [169]=Entity_InitWithTimeout,
    [231]=Script_OpenMemCardDialog, [232]=Script_ShowReceivedItem, [233]=Script_RunInvCommand,
    [132]=Script_SetScreenScrollPos, [133]=Script_SetCdRomSeekPos, [134]=Script_SetFogColor, [135]=Script_SetCdRomScreenPos, [136]=Script_ClearEntityFlag40, [155]=Script_SetGteMatrix, [156]=Entity_YieldOnLoading, [157]=Script_ScaleAnimValue, [166]=Script_ClampMenuRange,
    [124]=Script_FillObjEntrySlotValues, [125]=Script_SetObjEntrySlotValue,
    [126]=Script_SetObjEntryFlags, [127]=Script_ClearObjEntryFlags,
    [141]=Script_Noop1, [142]=Script_Noop2, [144]=Script_Noop4, [145]=Script_Noop3,
    [140]=Script_WaitFrameOrDraw, [143]=Entity_YieldOnDrawBusy,
    [50]=Entity_ClearCurrentFlag80, [55]=Entity_SetCurrentFlag4000, [57]=Entity_ClearCurrentFlag4000,
    [46]=Entity_SetCurrentActionMode, [47]=Script_SetEntityAnimFrame, [48]=Script_WaitAnimFrameMatch, [68]=Script_MoveTowardPoint,
    [69]=Script_GetAngleToEntity, [75]=Render_CameraSnapToEntity, [76]=Script_Noop, [78]=Script_SetEntityAnimSpeed,
    [81]=Scene_TestGlobalFlags, [82]=Scene_SetGlobalFlags, [83]=Scene_ClearGlobalFlags,
    [85]=Boot_MemCardStepInit,
    [89]=Entity_GetField, [139]=Entity_FindByTypeId,
    [90]=Entity_SetField,
    [111]=Script_InitEntityMoveState,
    [112]=Script_SetEntityAnim12Args, [183]=Script_SetEntityAnim5Args,
    [113]=Script_SetSceneEntryAnim,
    [114]=Script_SetRenderEntryEnabled, [115]=Script_SetRenderEntryPos,
    [154]=Script_SetMenuSlotEntry,
    [168]=Script_CountItemsByValue,
    [122]=Script_GetObjEntryField6, [187]=Script_SetEntryScrolled, [188]=Script_SetEntryMirrored,
    [175]=Entity_ResolveCurrentPosition, [176]=Script_SetBattleEntryCoords, [177]=Scene_SelectMapById, [178]=Script_GetFloorNumber, [179]=Script_SetFloorByEntityRoll,
    [180]=Script_SetMoveFlag, [181]=Script_ClearMoveFlag, [182]=Entity_PolarToPosition2,
    [207]=Script_SignalSceneExit, [208]=Script_DrawSaveOverlay,
    [209]=Script_ResetEntityAnimSlot,
    [106]=Script_GetEntityEffect, [107]=Pm_ScriptSendCommand0, [108]=Pm_ScriptSendCommand1,
    [98]=Entity_MarkNodeAnimPendingById, [99]=Entity_AdvanceNodeById, [100]=Script_RunEntityCommand,
    [197]=Entity_CopyAttachmentData, [198]=Script_SetEntityActionAndWait,
    [74]=Entity_TestCurrentFlags, [79]=Entity_SetCurrentFlag100, [80]=Entity_ClearCurrentFlag100,
    [185]=Script_ClearEntityFlag1, [186]=Script_SetEntityFlag1,
    [189]=Script_SetRenderFlag8Params, [190]=Script_ClearRenderFlag8,
    [191]=Script_SetRenderFlag10Value, [192]=Script_ClearRenderFlag10,
    [193]=Script_SetEntityFlag400, [194]=Script_ClearEntityFlag400, [195]=Script_WaitEntityFlags8,
    [109]=Pm_ScriptSetState, [110]=Pm_ScriptGetState,
    [151]=Pm_ScriptStopCurrentEntity, [152]=Script_SetEntityTarget, [153]=Script_ResetRenderState,
    [137]=Menu_SetModeState6, [138]=Menu_SetModeState5,
    [149]=Menu_ClearModeState, [150]=Menu_SetModeState8,
    [101]=Entity_ClearMotionVectors, [102]=Entity_ComputeVelocity, [84]=Entity_GetDistanceComponents, [91]=Script_ClearBattleFlags, [92]=Script_SetBattleFlags, [94]=Entity_GetPositionByType, [95]=Entity_MarkAnimPending, [96]=Entity_AdvancePendingAnim, [97]=Render_CameraTrackRelativeOffset, [123]=Script_SetObjEntryField8, [116]=Script_SetObjEntryLimit, [119]=Script_PointInPoly, [120]=Entity_ClearCurrentFlag20, [121]=Entity_SetCurrentFlag20, [117]=Script_SetFlagSlot, [118]=Render_CameraStepAngleFade,
    [128]=Script_SetObjEntryFlag80, [129]=Script_ClearObjEntryFlag80, [130]=Script_LoadGeomState, [131]=Script_WaitAnimRange, [146]=Script_PlayEntityAnim, [147]=Script_SetAnimInterpRate, [148]=Menu_GetModeState,
    [173]=Script_ClearInputFlagBit2, [174]=Script_SetInputFlagBit2,
    [172]=Script_SetAnimSlotPointer, [170]=Script_SetSceneFlag2000, [171]=Script_ClearSceneFlag2000,
    [204]=Script_ResetEntityRenderObj, [205]=Script_SnapPosToSpawnPos, [206]=Entity_SetFieldByTypeId, [203]=Script_GetAngleToPoint, [200]=Script_ClearEntityRoamFlag, [201]=Script_SetEntityRoamFlag,
    [212]=Script_SetEntityParentLink, [213]=Script_ClearEntityParentLink, [214]=Entity_ClearParentLink, [199]=Script_SetNodeFlag80, [196]=Script_RotateEntityAngle, [215]=Script_GetSin, [216]=Script_GetCos, [217]=Script_GetAtan2,
    [226]=Entity_CallAction, [227]=Script_SetInventorySlotPointer, [228]=Script_GetInventoryTotal, [229]=Script_GetInventorySlotLimit, [230]=Script_SetActorScreenPos,
    [220]=Script_SetEntityFlag1000000, [221]=Entity_PolarToPosition, [225]=Script_SetMeshFadeLevel, [222]=Entity_PopCount, [223]=Script_EnableEntityGravity, [224]=Script_DisableEntityGravity,
    [235]=Script_SetEntityShadowFlag, [236]=Script_ClearEntityShadowFlag,
    [184]=Script_TurnTowardPointStep, [234]=Akao_ProcessCommand, [237]=Script_DispatchScriptCmd, [238]=Script_BeginSceneTransition, [239]=Script_OpenSaveLoadMenu, [240]=Script_GetPlayerInputFlag
};
