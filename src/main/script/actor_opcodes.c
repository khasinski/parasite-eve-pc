#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_node_types.h"
#endif
#include "common.h"
#include "pe_hooks.h"
#include "pe1/akao.h"
#include "pe1/akao/globals.h"
#include "pe1/draw_state.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/psyq_press.h"
#include "pe1/akao_send.h"
#include "pe1/akao/commands.h"
#include "pe1/asset_find.h"
#include "pe1/asset_table.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_level_up.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_start.h"
#include "pe1/boot_disc_check.h"
#include "pe1/cdrom.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/entity_frame_update.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/font.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/gte_types.h"
#include "pe1/map_selection.h"
#include "pe1/memcard.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_item_record.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_cd.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_lighting.h"
#include "pe1/render_setup.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_room_load.h"
#include "pe1/scene_transition.h"
#include "pe1/script_node.h"
#include "pe1/script_state.h"
#include "pe1/script_queue.h"

/* Shared native state; external variables have one storage definition. */

/* Adjacent field-actor query and distance routines share one compiled unit. */

/* Script op: look up an actor by (type id, sub id) script args and read one
 * of its fields into the out arg. args = script argument pointer array:
 * args[0]=field selector, args[1]=type id, args[2]=sub id, args[3]=out. */
s32 Entity_QueryField(s32 *args[]) {
    s32 key;
    s32 sel;
    FieldActor *node;

    key = *args[1];
    if (key == 0) {
        FieldActor *tmp = g_PlayerEntity;
        if (tmp == NULL) {
            *args[3] = -1;
            return 1;
        }
        node = tmp;
    } else {
        node = g_CurrentEntity;
        if ((key != node->type_id) || (*args[2] != node->sub_id)) {
            node = g_FieldActorListHead;
            if (node != NULL) {
                s32 k = *args[1];
                while (node != NULL && ((node->type_id != k) ||
                       (node->sub_id != *args[2]) || (node->flags & 0x10))) {
                    node = node->next;
                }
            }
            if (node == NULL) {
                *args[3] = -1;
                return 1;
            }
        }
    }
    sel = *args[0];
    switch (sel) {
    case 0:
        *args[3] = node->mode;
        break;
    case 1:
        *args[3] = node->flags;
        break;
    case 2:
        *args[3] = (s16)node->anim.parts.integer;
        break;
    case 3:
        *args[3] = node->action;
        break;
    default:
        return 1;
    }
    return 1;
}

s32 Entity_GetDistanceComponents(s32 *args[]) {
    s32 *temp_a0;
    s32 *temp_t0;
    s32 temp_a1;
    s32 temp_a2;
    s32 temp_a2_2;
    s32 temp_a3;
    s32 temp_a3_2;
    s32 temp_v0;
    s32 temp_v0_2;
    s32 temp_v0_3;
    s32 temp_v1;
    s32 temp_v1_2;
    s32 temp_v1_3;
    s32 var_v0;
    s32 var_v0_2;
    s32 var_v0_3;
    int sourceX;
    s32 key0;
    FieldActor *node;

    key0 = *args[0];
    if (key0 == 0) {
        FieldActor *tmp;
        tmp = g_PlayerEntity;
        if (tmp == NULL) {
            *args[2] = -1;
            return 1;
        }
        node = tmp;
    } else {
        node = g_FieldActorListHead;
        if (node != NULL) {
            s32 key;
            key = key0;
            while (node != NULL && ((node->type_id != key) ||
                   (node->sub_id != *args[1]) || (node->flags & 0x10))) {
                node = node->next;
            }
        }
        if (node == NULL) {
            *args[2] = -1;
            return 1;
        }
    }

    {
        FieldActor *state;
        s32 node_x;
        s32 *out;
        state = g_CurrentEntity;
        sourceX = state->pos_x;
        node_x = node->pos_x;
        out = args[2];
        var_v0 = sourceX;
        var_v0 = var_v0 - node_x;
        if (var_v0 < 0) {
            var_v0 = node_x - sourceX;
        }
        *out = var_v0;
    }
    {
        FieldActor *state;
        s32 *out;
        state = g_CurrentEntity;
        out = args[2];
        temp_a3_2 = state->pos_y;
        temp_v0_2 = node->pos_y;
        temp_a2 = *out;
        temp_v1_2 = temp_a3_2 - temp_v0_2;
        if (temp_v1_2 >= 0) {
            var_v0_2 = temp_a2 + temp_v1_2;
        } else {
            var_v0_2 = temp_a2 + (temp_v0_2 - temp_a3_2);
        }
        *out = var_v0_2;
    }
    {
        FieldActor *state;
        s32 *out;
        state = g_CurrentEntity;
        out = args[2];
        temp_a2_2 = state->pos_z;
        temp_v0_3 = node->pos_z;
        temp_a1 = *out;
        temp_v1_3 = temp_a2_2 - temp_v0_3;
        if (temp_v1_3 >= 0) {
            var_v0_3 = temp_a1 + temp_v1_3;
        } else {
            var_v0_3 = temp_a1 + (temp_v0_3 - temp_a2_2);
        }
        *out = var_v0_3;
    }
    return 1;
}



/* Original battle-entry opcode 85; the historical name also covers the
 * menu-background transition shared with card screens. */
s32 Boot_MemCardStepInit(int **arg0) {
    Pe1GameState *base;
    int status;

    base = &g_GameState;
    loop_1:
    switch (base->memcard_init_phase) {
    case 0x0:
        if (!(base->story_day_flags & 3)) {
            base->memcard_init_phase = 0x37U;
        }
        base->flags = base->flags | 0x800000;
        break;
    case 0x37:
        if (!(g_GameState.flags & 0x400000)) {
            MemCard_InitSlotState();
        }
        base->memcard_init_phase = 0x38U;
        goto loop_1;
    case 0x38:
        status = CdRom_StepReadState(1);
        if (status < 0) return status;
        if (status != 1) {
            if (!(g_GameState.flags & 0x400000)) {
                Menu_SaveBgStartFadeOut();
            }
            base->memcard_init_phase = 0x39U;
            goto loop_1;
        }
        break;
    case 0x39:
        status = Asset_LoadTimTextures(1);
        if (status < 0) return status;
        if (status != 1) {
            base->memcard_init_phase = 0x3AU;
            goto loop_1;
        }
        break;
    case 0x3A:
        g_GameStateFlags |= 2;
        Battle_StartEncounter((u8)**arg0);
        base->memcard_init_phase = 0x3BU;
        break;
    case 0x3B:
        if (base->story_day_flags & 3) {
            break;
        }
        base->memcard_init_phase = 0U;
        base->flags = base->flags & 0xFF7FFFFF;
        /* fall through */
    default:
        return 1;
    }
    g_ScriptCursorAddress -= 0xC;
    g_ScriptNodePool->active = 1;
    return 0;
}

int Entity_GetPositionByType(int **args) {
    FieldActor *entity;
    FieldActor *player;
    int selector;
    int entity_id;

    selector = *args[1];
    if (selector == 0) {
        player = g_PlayerEntity;
        if (player == 0) {
            *args[6] = -1;
            return 1;
        }
        entity = player;
    } else {
        entity = g_FieldActorListHead;
        entity_id = selector;
        while (entity != 0) {
            if (entity->type_id == entity_id &&
                entity->sub_id == *args[2] &&
                (entity->flags & 0x10) == 0) {
                break;
            }
            entity = entity->next;
        }
        if (entity == 0) {
            *args[6] = -1;
            return 1;
        }
    }

    *args[6] = 1;
    switch (*args[0]) {
    case 0:
        *args[3] = entity->pos_x;
        *args[4] = entity->pos_y;
        *args[5] = entity->pos_z;
        break;
    case 1:
        *args[3] = entity->base_x;
        *args[4] = entity->base_y;
        *args[5] = entity->base_z;
        break;
    case 2:
        *args[3] = entity->motion_x;
        *args[4] = entity->motion_y;
        *args[5] = entity->motion_z;
        break;
    case 3:
        *args[3] = entity->accel_x;
        *args[4] = entity->accel_y;
        *args[5] = entity->accel_z;
        break;
    case 4:
        *args[3] = entity->gravity_x;
        *args[4] = entity->gravity_y;
        *args[5] = entity->gravity_z;
        break;
    case 5:
        *args[3] = entity->rot_x;
        *args[4] = entity->rot_y;
        *args[5] = entity->rot_z;
        break;
    case 6:
        *args[3] = entity->delta_x;
        *args[4] = entity->delta_y;
        *args[5] = entity->delta_z;
        break;
    }
    return 1;
}


int Render_CameraTrackRelativeOffset(int **args) {
    FieldActor *speed_entity;
    FieldActor *motion_entity;
    ScriptNode *init_state;
    ScriptNode *camera_state;
    int movement_speed;
    int turn_speed;
    int target_angle;
    int desired_angle;
    int current_angle;
    int angle_difference;
    int wrap_difference;
    int current_x;
    int current_z;
    int target_x;
    int target_z;
    int movement_z;
    int remaining_distance;
    int movement_distance;

    speed_entity = g_CurrentEntity;
    current_x = speed_entity->pos_x;
    current_z = speed_entity->pos_z;
    if (speed_entity != g_PlayerEntity) {
        movement_speed = speed_entity->move_factor;
    } else {
        movement_speed = Math_FixedMul(0x50000, speed_entity->move_factor);
    }
    movement_speed = Math_FixedMul(movement_speed, g_CurrentEntity->move_speed << 4);

    init_state = g_ScriptNodePool;
    if ((init_state->flags & 0x20) == 0) {
        turn_speed = *args[2];
        target_x = current_x + *args[0];
        target_z = current_z + *args[1];
        init_state->flags |= 0x20;
        init_state->target20 = 0;
        init_state->target14 = turn_speed;
        init_state->target1c = target_z;
        init_state->target18.coordinate = target_x;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(init_state, PE_NODE_COORDINATE);
#endif
    } else {
        turn_speed = init_state->target14;
        target_x = init_state->target18.coordinate;
        target_z = init_state->target1c;
    }

    target_angle = (u16)(0x1400 - ratan2(current_z - target_z,
                                             current_x - target_x));
    target_angle &= 0xFFF;
    desired_angle = target_angle;
    if (turn_speed != 0) {
        current_angle = g_CurrentEntity->rot_y;
        if (current_angle < desired_angle) {
            angle_difference = desired_angle - current_angle;
            if (angle_difference < 0x800) {
                if (turn_speed >= angle_difference) {
                    target_angle = desired_angle;
                } else {
                    target_angle = current_angle + turn_speed;
                }
            } else if (turn_speed >= angle_difference) {
                target_angle = desired_angle;
            } else {
                target_angle = current_angle - turn_speed;
                if (target_angle < 0) {
                    wrap_difference = current_angle + 0x1000;
                    wrap_difference -= desired_angle;
                    if (wrap_difference < turn_speed) {
                        target_angle = desired_angle;
                    }
                }
            }
        } else {
            angle_difference = current_angle - desired_angle;
            if (angle_difference < 0x800) {
                if (turn_speed >= angle_difference) {
                    target_angle = desired_angle;
                } else {
                    target_angle = current_angle - turn_speed;
                }
            } else if (turn_speed >= angle_difference) {
                target_angle = desired_angle;
            } else {
                target_angle = current_angle + turn_speed;
                if (target_angle >= 0x1001) {
                    wrap_difference = desired_angle + 0x1000;
                    wrap_difference -= current_angle;
                    if (wrap_difference < turn_speed) {
                        target_angle = desired_angle;
                    }
                }
            }
        }
        target_angle &= 0xFFF;
        g_CurrentEntity->rot_y = target_angle;
    }

    /* MIPS shifts the word bits even when the sine/cosine is negative. */
    PE_HOOK(PE_HOOK_ACTOR_STEP, (&(PeHookActorStep){speed_entity, &movement_speed, NULL}));
    g_CurrentEntity->motion_x = Math_FixedMul(-movement_speed, (s32)((u32)rsin(target_angle) << 4));
    movement_z = Math_FixedMul(-movement_speed, (s32)((u32)rcos(target_angle) << 4));
    current_x = (target_x - current_x) >> 16;
    current_z = (target_z - current_z) >> 16;
    remaining_distance = current_x * current_x + current_z * current_z;
    motion_entity = g_CurrentEntity;
    current_x = Pe1Fixed_Integer(&motion_entity->motion_x);
    current_z = movement_z >> 16;
    movement_distance = current_x * current_x + current_z * current_z;
    motion_entity->motion_z = movement_z;
    if (movement_distance >= remaining_distance) {
        camera_state = g_ScriptNodePool;
        motion_entity->pos_x = target_x;
        motion_entity->pos_z = target_z;
        motion_entity->motion_x = 0;
        motion_entity->motion_z = 0;
        camera_state->flags &= 0xFFDF;
        return 1;
    } else {
        g_ScriptCursor -= 5;
        g_ScriptNodePool->active = 1;
        return 0;
    }
}

/* Script op: turn the current actor's yaw to the angle in arg0[0] by the
 * step in arg0[1], picking the direction of the shorter arc on the first
 * call; yields until the next step would reach or pass the target. The
 * angles are shorts in retail: their extensions survive at every compare. */
s32 Render_CameraStepAngleFade(s32 **arg0) {
    short current;
    short target;
    short step;
    ScriptNode *node;
    int flags;

    current = g_CurrentEntity->rot_y & 0xFFF;
    node = g_ScriptNodePool;
    flags = node->flags;
    if (!(flags & 0x20)) {
        short distance;

        target = *arg0[0] & 0xFFF;
        step = *(u16 *)arg0[1];
        if (current == target) {
            return 1;
        }
        node->flags = flags | 0x20;
        if (target - current >= 0) {
            distance = target - current;
        } else {
            distance = current - target;
        }
        if (target < current) {
            if (distance >= 0x801) {
                g_ScriptNodePool->target14 = step;
            } else {
                g_ScriptNodePool->target14 = -step;
            }
        } else if (distance < 0x801) {
            g_ScriptNodePool->target14 = step;
        } else {
            g_ScriptNodePool->target14 = -step;
        }
        g_ScriptNodePool->target18.coordinate = target;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(g_ScriptNodePool, PE_NODE_COORDINATE);
#endif
    }
    step = g_ScriptNodePool->target14;
    target = g_ScriptNodePool->target18.coordinate;
    {
        int angle = current;
        int goal = target;

        if ((angle >= goal && goal >= angle + step) ||
            (goal >= angle && angle + step >= goal)) {
            g_CurrentEntity->rot_y = target;
            g_ScriptNodePool->flags &= 0xFFDF;
            return 1;
        }
    }
    if ((step > 0 && current + step >= 0x1001 && ((current + step) & 0xFFF) >= target) ||
        (step < 0 && current + step < 0 && ((current + step) & 0xFFF) <= target)) {
        g_CurrentEntity->rot_y = target;
        g_ScriptNodePool->flags &= 0xFFDF;
        return 1;
    }
    g_ScriptCursorAddress -= 0x10;
    g_CurrentEntity->rot_y += step;
    g_ScriptNodePool->active = 1;
    return 0;
}

void ResetGraph(s32);
void SetDispMask(s32);


int Script_PointInPoly(int **arg0) {
    PolygonVertex local[4];
    unsigned int i;

    for (i = 0; i < 4; i++) {
        local[i].xFraction = (u16)*arg0[i * 2];
        local[i].x = (s16)((u32)*arg0[i * 2] >> 16);
        local[i].zFraction = (u16)*arg0[i * 2 + 1];
        local[i].z = (s16)((u32)*arg0[i * 2 + 1] >> 16);
    }

    *arg0[10] = Geo_PointInPoly(*arg0[8], *arg0[9], local, 4);
    return 1;
}

/* Scene audio slots (bit per slot) a script started while a movie plays. */
static u8 g_MovieMusicSlots;
#if PE_NATIVE_STATE_CPU_COMPONENTS
int PeStateScript_TransientIdle(void){return g_MovieMusicSlots==0;}
#endif

/* Played through, music started with a movie ends with it; a skipped movie
 * stops it the way script op 0xC9 does. */
void Script_StopMovieMusic(void) {
    int i;

    for (i = 0; i < 2; i++) {
        if (g_MovieMusicSlots & (1 << i)) {
            Akao_Cmd_11(g_GameState.scene_audio.tracks.keys[i][1]);
        }
    }
    g_MovieMusicSlots = 0;
}

/* Original movie opcode: decoder/player code is statically linked.
 * The existing native work area replaces the two executable overlay loads. */
int Script_LoadSceneData(int **args) {
    if (!args || !args[0] || !g_CdImage || !g_CdLoadAreaBase ||
        (uintptr_t)g_CdLoadAreaBase % 16) return -1;
    int number=(s16)*args[0];
    if (number<0 || number>=47) return -1;
    if (g_DrawEnabled.bits) return -2;
    u32 flags=g_GameState.flags;
    Pe1SceneAudioState audio=g_GameState.scene_audio;
    CdlATV mix=g_AkaoCdVolume;
    u8 *buffers[]={g_CdLoadAreaBase};
    g_GameState.flags |= 0x8200;
    DrawSync(0);
    VSync(0);
    SetDispMask(0);
    ResetGraph(1);
    /* Reloading the PS1 player used to reset these movie-local values. */
    g_MenuCardVideoDepthFlip=0;
    g_MenuCardVideoSize[0]=g_MenuCardVideoSize[1]=0;
    g_MovieMusicSlots=0;
    if (!MenuCardFirst_InitVideoBuffers(1,buffers) || !MenuCardFirst_OpenVideo(number)) {
        DecDCToutCallback(NULL);
        MenuCardFirst_ResetDisplayState();
        g_GameState.flags=flags;
        g_GameState.scene_audio=audio;
        g_AkaoCdVolume=mix;CdMix(&mix);
        return -3;
    }
    MenuCardFirst_RequestVideoDepthFlip(1);
    return 1;
}

int Script_PlayEntityAnim(int **args) {
    ScriptNode *script_state;
    FieldActor *actor;
    FieldActor *post_actor;
    FieldActor *player;
    u32 control_flags;
    u16 progress;

    script_state = g_ScriptNodePool;
    progress = script_state->flags;
    if (progress & 0x20) {
        if (!(g_CurrentEntity->render_object.flags_9C & 4)) {
            script_state->flags = progress & ~0x20;
            return 1;
        }
    } else {
        actor = g_CurrentEntity;
        actor->flags &= ~0x40;
        script_state->flags |= 0x20;
        Anim_SetInterpRate(&actor->render_object, (s16)*args[0]);
        post_actor = g_CurrentEntity;
        player = g_PlayerEntity;
        post_actor->render_object.flags_9C |= 4;

        if (post_actor == player) {
            control_flags = g_FieldMoveLock;
            control_flags &= ~2;
            g_FieldMoveLock = control_flags;
            Anim_SetInterpRate(&g_PlayerBattleEntity, (s16)*args[0]);
            g_PlayerBattleEntity.flags_9C |= 4;
        }
    }

    g_ScriptCursor -= 3;
    g_ScriptNodePool->active = 1;
    return 0;
}

int Script_CopyEntityRenderObject(int **args) {
    FieldActor *source;
    int type_id;

    type_id = *args[0];
    if (type_id == 0) {
        FieldActor *player;

        player = g_PlayerEntity;
        if (player == 0) {
            return 1;
        }
        source = player;
    } else {
        source = g_FieldActorListHead;
        if (source == 0) {
            return 1;
        }

        {
            int wanted_type;

            wanted_type = type_id;

            do {
                if (source->type_id != wanted_type) {
                    source = source->next;
                } else if (source->sub_id != *args[1]) {
                    source = source->next;
                } else if ((source->flags & 0x10) == 0) {
                    break;
                } else {
                    source = source->next;
                }
            } while (source != 0);
        }

        if (source == 0) {
            return 1;
        }
    }

    Render_InitObjectFromTable(&g_CurrentEntity->render_object,
                               &source->render_object, (s16)*args[2]);
    Render_TransformSkinnedVertices(&g_CurrentEntity->render_object,
                                    g_EntityRenderScratch);

    g_CurrentEntity->parent = source;
    g_CurrentEntity->pos_x =
        (s32)((u32)(s16)g_CurrentEntity->render_object.rotation_overrides[0].x << 16);
    g_CurrentEntity->pos_y =
        (s32)((u32)(s16)g_CurrentEntity->render_object.rotation_overrides[0].y << 16);
    g_CurrentEntity->pos_z =
        (s32)((u32)(s16)g_CurrentEntity->render_object.rotation_overrides[0].z << 16);
    g_CurrentEntity->flags |= 0x2000;

    return 1;
}

/* Script opcodes: draw the current actor's model through a caller-supplied
 * GTE matrix, the render-mode switch wait that pops the script cursor, and
 * the angle from the current actor to a named actor. Contiguous handlers;
 * the angle opcode reaches its actors through array views, absolute under
 * -G8 as well. */

int Script_SetGteMatrix(int **args) {
    FieldActor *setup_actor;
    FieldActor *matrix_actor;
    FieldActor *color_actor;
    FieldActor *render_actor;
    FieldActor *flag_actor;
    GteMatrix scale_matrix;
    GteMatrix *translation_matrix;
    int first_draw_slot;
    int second_draw_slot;
    int result;
    u32 *script_ptr;
    ScriptNode *script_state;
    u32 flags;

    setup_actor = g_CurrentEntity;
    setup_actor->render_object.model_matrix.translation[0] = Pe1Fixed_Integer(&setup_actor->pos_x);
    setup_actor->render_object.model_matrix.translation[1] = Pe1Fixed_Integer(&setup_actor->pos_y);
    setup_actor->render_object.model_matrix.translation[2] = Pe1Fixed_Integer(&setup_actor->pos_z);
    setup_actor->render_object.table_value2c = (u16)setup_actor->rot_x;
    setup_actor->render_object.table_value2e = (u16)setup_actor->rot_y;
    setup_actor->render_object.table_value30 = (u16)setup_actor->rot_z;
    RotMatrix((GteShortVector *)&setup_actor->render_object.table_value2c,
              (GteMatrix *)&setup_actor->render_object.model_matrix);

    /* Scale the rotation by the actor's move speed: model = rotation * diag(speed). */
    matrix_actor = g_CurrentEntity;
    scale_matrix.m[0][0] = matrix_actor->move_speed;
    scale_matrix.m[1][1] = matrix_actor->move_speed;
    scale_matrix.m[2][2] = matrix_actor->move_speed;
    scale_matrix.t[2] = 0;
    scale_matrix.t[1] = 0;
    scale_matrix.t[0] = 0;
    scale_matrix.m[2][1] = 0;
    scale_matrix.m[2][0] = 0;
    scale_matrix.m[1][2] = 0;
    scale_matrix.m[1][0] = 0;
    scale_matrix.m[0][2] = 0;
    scale_matrix.m[0][1] = 0;

    gte_MulMatrix0(&matrix_actor->render_object.model_matrix, &scale_matrix,
                   &g_CurrentEntity->render_object.model_matrix);

    translation_matrix = (GteMatrix *)&g_CurrentEntity->render_object.model_matrix;
    gte_ldtransmatrix(translation_matrix);
    gte_ldv0_word3_at(scale_matrix.t);
    gte_cop2_hazard_slot();
    gte_cop2_hazard_slot();
    gte_rtv0tr_sf0();
    gte_swc2_9_0(translation_matrix->t);
    gte_swc2_10_4(translation_matrix->t);
    gte_swc2_11_8(translation_matrix->t);
    Pgxp_StoreModelMatrix(&g_CurrentEntity->render_object.model_matrix,
                          setup_actor->rot_x, setup_actor->rot_y, setup_actor->rot_z,
                          matrix_actor->move_speed);

    color_actor = g_CurrentEntity;
    color_actor->render_object.shade = *args[0];
    g_CurrentEntity->render_object.lightNegativeY = *args[1];
    g_CurrentEntity->render_object.lightPositiveY = *args[2];

    Render_InitRoomPrimState(&g_CurrentEntity->render_object);
    render_actor = g_CurrentEntity;
    Anim_BuildRotationMatrices(&render_actor->render_object,
                               render_actor->action_data, 0);
    Render_TransformVertices(&g_CurrentEntity->render_object);
    Render_TransformSkinnedVertices(&g_CurrentEntity->render_object, g_EntityRenderScratch);
    Render_DrawObject(&g_CurrentEntity->render_object, &g_RenderLightingState);
    Render_UpdateClutTable(&g_CurrentEntity->render_object, 1,
                           (s16)g_ActiveDrawSlot);

    flags = (flag_actor = g_CurrentEntity)->flags;
    if ((flags & 0x10000000) != 0) {
        first_draw_slot = g_ActiveDrawSlot;
        first_draw_slot ^= 1;
        g_ActiveDrawSlot = first_draw_slot;
        Render_DrawObject(&flag_actor->render_object, &g_RenderLightingState);
        Render_UpdateClutTable(&g_CurrentEntity->render_object, 1,
                               (s16)g_ActiveDrawSlot);
        second_draw_slot = g_ActiveDrawSlot;
        second_draw_slot ^= 1;
        g_ActiveDrawSlot = second_draw_slot;
    } else {
        if ((flags & 0x08000000) == 0) {
        result = 0;
        script_ptr = g_ScriptCursor;
        flag_actor->flags = flags | 0x08000000;
        script_state = g_ScriptNodePool;
        g_ScriptCursor = script_ptr - 5;
        script_state->active = 1;
        return result;
        }
        flag_actor->flags = flags & 0xF7FFFFFF;
    }
    return 1;
}


int Boot_CheckModeSwitch(int **unused) {
    (void)unused;
    int mode = g_GeomScroll.state.flags & 7;
    uintptr_t cursor;
    ScriptNode *node;

    if (mode == 4 || mode == 0)
        return 1;
    cursor = g_ScriptCursorAddress;
    node = g_ScriptNodePool;
    cursor -= 8;
    g_ScriptCursorAddress = cursor;
    node->active = 1;
    return 0;
}


/* Script op: angle from the current entity to the actor named by
 * (args[0], args[1]), relative to the current entity's yaw, into args[2]. */
s32 Script_GetAngleToEntity(s32 *args[]) {
    s32 key;
    s32 key2;
    FieldActor *node;
    s32 angle;
    s32 dx;
    s32 dz;

    key = *args[0];
    if (key == 0) {
        FieldActor *tmp;

        tmp = g_PlayerEntity;
        if (tmp == NULL) {
            *args[2] = -1;
            return 1;
        }
        node = tmp;
    } else {
        key2 = key;
        node = g_FieldActorListHead;
        if (node == NULL) {
            *args[2] = -1;
            return 1;
        }
        do {
            if ((node->type_id != key2) ||
                (node->sub_id != *args[1]) ||
                (node->flags & 0x10)) {
                node = node->next;
            } else {
                break;
            }
        } while (node != NULL);
        if (node == NULL) {
            *args[2] = -1;
            return 1;
        }
    }

    {
        FieldActor *state = g_CurrentEntity;
        dx = state->pos_x - node->pos_x;
        dz = (state->pos_z - node->pos_z) >> 16;
    }
    angle = 0x1400 - ratan2(dz, dx >> 16);
    if (angle >= 0x1001) {
        angle -= 0x1000;
    }
    angle -= g_CurrentEntity->rot_y;
    if (angle < 0) {
        angle += 0x1000;
    }
    *args[2] = angle;
    return 1;
}

/* Resident map selection data used by the original floor scripts. */
MapSelectionIndexRow g_MapSelectIndexTable[7] = {
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23}},
    {{24, 1, 2, 3, 4, 5, 6, 7, 8, 25, 26, 11, 12, 13, 14, 15, 16, 17, 18, 19, 27, 28, 29, 30}},
    {{31, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 11, 12, 13, 14, 15, 16, 17, 18, 19, 34, 35, 36, 37}},
    {{38, 1, 2, 3, 4, 5, 6, 7, 8, 39, 40, 11, 12, 13, 14, 15, 16, 17, 18, 19, 41, 42, 43, 44}},
    {{45, 1, 2, 3, 4, 5, 6, 7, 8, 46, 47, 11, 12, 13, 14, 15, 16, 17, 18, 19, 48, 49, 50, 51}},
    {{52, 1, 2, 3, 4, 5, 6, 7, 8, 53, 54, 11, 12, 13, 14, 15, 16, 17, 18, 19, 55, 56, 57, 58}},
    {{59, 1, 2, 3, 4, 5, 6, 7, 8, 60, 61, 11, 12, 13, 14, 15, 16, 17, 18, 19, 62, 63, 64, 65}},
};

MapSelectionName g_MapFilenameTable[66] = {
    {"m0291i"},
    {"m0296i"},
    {"m0297i"},
    {"m0292i"},
    {"m0293i"},
    {"m0294i"},
    {"m0295i"},
    {"m0295i"},
    {"m0295i"},
    {"m0298i"},
    {"m0299i"},
    {"m0300i"},
    {"m0301i"},
    {"m0302i"},
    {"m0303i"},
    {"m0335i"},
    {"m0304i"},
    {"m0305i"},
    {"m0306i"},
    {"m0307i"},
    {"m0311i"},
    {"m0310i"},
    {"m0309i"},
    {"m0308i"},
    {"m0383i"},
    {"m0384i"},
    {"m0385i"},
    {"m0389i"},
    {"m0388i"},
    {"m0387i"},
    {"m0386i"},
    {"m0390i"},
    {"m0391i"},
    {"m0392i"},
    {"m0396i"},
    {"m0395i"},
    {"m0394i"},
    {"m0393i"},
    {"m0397i"},
    {"m0398i"},
    {"m0399i"},
    {"m0403i"},
    {"m0402i"},
    {"m0401i"},
    {"m0400i"},
    {"m0404i"},
    {"m0405i"},
    {"m0406i"},
    {"m0410i"},
    {"m0409i"},
    {"m0408i"},
    {"m0407i"},
    {"m0411i"},
    {"m0412i"},
    {"m0413i"},
    {"m0417i"},
    {"m0416i"},
    {"m0415i"},
    {"m0414i"},
    {"m0418i"},
    {"m0419i"},
    {"m0420i"},
    {"m0424i"},
    {"m0423i"},
    {"m0422i"},
    {"m0421i"},
};

s8 g_MapIdTable[40] = {109, 48, 50, 57, 53, 105, 0, 0, 109, 48, 50, 57, 53, 105, 0, 0, 109, 48, 50, 57, 53, 105, 0, 0, 109, 48, 50, 57, 53, 105, 0, 0, 109, 48, 50, 57, 53, 105, 0, 0};
s8 g_MapNameOverrideA[8] = {109, 48, 50, 57, 48, 105, 0, 0};
s8 g_MapNameOverrideB[8] = {109, 48, 51, 49, 50, 105, 0, 0};

int Draw_PackPrimColor(int start, int end);

int Scene_SelectMapById(int **args) {
    int raw_selection;
    u8 selection;
    u8 state;
    MapSelectionIndexRow *select_index;
    u32 *game_state;
    u32 map_flags;
    u32 state_flags;

    if ((u8)Menu_IsEquipSlotActive() != 0) {
        raw_selection = Render_GetOrLoadFontGlyph((u8)*args[0]);
    } else {
        raw_selection = Render_LoadFontGlyphAlt();
    }
    selection = raw_selection;

    if (selection == 0xFF) {
        state = Menu_GetEquipSlotStateOrIndex();
        if (state < 2) {
            Menu_ResetEquipSlotState();
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapNameOverrideA);
        } else {
            Menu_ResetEquipSlotState();
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapNameOverrideB);
        }
        return 1;
    }

    if ((u8)(raw_selection - 6) < 2 || selection == 8) {
        if ((s16)Draw_PackPrimColor(0, 100) < 60) {
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapIdTable);
        } else {
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapIdTable + ((s16)Draw_PackPrimColor(1, 4) << 3));
        }
    } else {
        state = (u8)(Menu_GetEquipSlotStateOrIndex() - 1);
        state /= 10;
        select_index = g_MapSelectIndexTable + state;
        g_SceneDispatchToken = Text_ParseBase32Id(
            g_MapFilenameTable[select_index->filename_index[selection]].name);
    }

    raw_selection = 1;
    if (g_SceneDispatchCur == g_SceneDispatchToken) {
        game_state = &g_GameState.flags;
        state_flags = g_GameStateFlags;
        map_flags = game_state[0];
        g_GameStateFlags = state_flags | 0x2000;
        game_state[0] = map_flags | 0x800;
    } else {
        return raw_selection;
    }
    return raw_selection;
}

int Script_SetFloorByEntityRoll(int **args) {
    int selection;
    u8 state;
    MapSelectionIndexRow *select_index;
    u32 *game_state;
    u32 map_flags;
    u32 state_flags;

    if (!(Menu_IsEquipSlotActive() & 0xFF)) {
        Render_LoadFontGlyphAlt();
    }
    selection = Render_SetFontGlyphByCode((u8)*args[0]) & 0xFF;

    if (selection == 0xFF) {
        if ((u8)Menu_GetEquipSlotStateOrIndex() < 2) {
            Menu_ResetEquipSlotState();
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapNameOverrideA);
        } else {
            Menu_ResetEquipSlotState();
            g_SceneDispatchToken = Text_ParseBase32Id(g_MapNameOverrideB);
        }
        return 1;
    }

    state = (u8)(Menu_GetEquipSlotStateOrIndex() - 1);
    state /= 10;
    select_index = g_MapSelectIndexTable + state;
    g_SceneDispatchToken =
        Text_ParseBase32Id(
            g_MapFilenameTable[select_index->filename_index[selection]].name);

    selection = 1;
    if (g_SceneDispatchCur == g_SceneDispatchToken) {
        game_state = &g_GameState.flags;
        state_flags = g_GameStateFlags;
        map_flags = game_state[0];
        g_GameStateFlags = state_flags | 0x2000;
        game_state[0] = map_flags | 0x800;
    } else {
        return selection;
    }
    return selection;
}

/* Script opcodes: actor screen position, start-up memory-card dialog,
 * received-item notice, inventory command and the AKAO/CD sound command.
 * Contiguous handlers between the map-selection flow and the ASPSX 2.70
 * script dispatcher; the screen-position opcode touches no data, so -G8
 * leaves it unchanged. */

int Render_SetEntryPosition(int index, int x, int y);

int Script_SetActorScreenPos(int **arg0) {
    Render_SetEntryPosition(*arg0[0], (s16)*arg0[1], (s16)*arg0[2]);
    return 1;
}

int Script_OpenMemCardDialog(int **unused) {
    Pe1GameState *state = &g_GameState;
    ScriptNode *node;
    unsigned short *node_flags;
    int flags;
    uintptr_t cursor;

    if (!(state->flags & 0x1000)) {
        node = g_ScriptNodePool;
        node_flags = &node->flags;
        flags = *node_flags;
        if (!(flags & 0x20)) {
            *node_flags = flags | 0x20;
            cursor = g_ScriptCursorAddress;
            node->active = 1;
            cursor -= 8;
            g_ScriptCursorAddress = cursor;
            return 0;
        }
        Render_BeginSceneLoad();
        Menu_OpenStartupMemCardDialog();
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
    return 1;
}

int Script_ShowReceivedItem(int **arg0) {
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
    if (!(flags & 0x20)) {
    *node_flags = flags | 0x20;
    cursor = g_ScriptCursorAddress;
    node->active = 1;
    cursor -= 0xC;
    g_ScriptCursorAddress = cursor;
    return 0;

    }
    Render_BeginSceneLoad();
    Menu_CreateItemUsePanel(**saved);
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
    return 1;
}

int Script_RunInvCommand(int **args) {
    ScriptNode *node;
    int flags;
    int result;

    if ((g_ScriptNodePool->flags & 0x20) == 0) {
        *args[3] = Inv_DispatchCommand(*args[0], *args[1], *args[2], args[4]);
    }

    if (MenuWidget_HasActiveNodes() != 0) {
        node = g_ScriptNodePool;
        flags = node->flags;
        if ((flags & 0x20) == 0) {
            node->flags = flags | 0x20;
            g_GameState.flags |= 0x9000;
            Render_BeginSceneLoad();
        } else {
            g_GameStateFlags |= 4;
        }
        g_ScriptCursorAddress -= 0x1C;
        g_ScriptNodePool->active = 1;
        return 0;
    }

    result = g_MenuFrameCommand;
    if (result != 0) {
        *args[3] = result;
        g_ScriptNodePool->flags &= 0xFFDF;
    }
    return 1;
}

/* The sound archive's second word locates a section whose asset-table
 * word packs the entry count (bits 22-31) and the table's offset from the
 * archive (bits 0-21). */
typedef struct AkaoArchiveHeader {
    u32 reserved;
    u32 section_offset;
} AkaoArchiveHeader;

typedef struct AkaoArchiveSection {
    u8 reserved[0x30];
    u32 asset_table;
} AkaoArchiveSection;


typedef struct AkaoAssetEntry {
    u8 pad0[3];
    u8 flags;
    u32 data_offset;
    u8 value8;
    u8 pad9;
    u16 id;
} AkaoAssetEntry;

PE1_STATIC_ASSERT(sizeof(AkaoAssetEntry) == 12, actor_sound_asset_record_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(AkaoAssetEntry, id) == 10, actor_sound_asset_id_offset);

typedef struct AkaoStackLocals {
    int slot;
    u8 pad4[4];
    s8 volumes[4];
    u8 pad12[8];
} AkaoStackLocals;



int Akao_ProcessCommand(AkaoCommandArgs args) {
    AkaoStackLocals locals;
    int status;
    int handle;
    int wait_one;
    int slot_index0;
    int slot_index1;
    int slot_index2;
    int count;
    u32 entry_count;
    u32 entry_limit;
    int volume;
    u32 table_info;
    FieldActor *actor;
    u8 *base;
    FieldActor *player;
    AkaoAssetEntry *current;
    AkaoAssetEntry *entries;
    DeferredAkaoCommand *deferred;
    u8 *deferred_count;

    switch (*args[0]) {
    case 0x64:
        Akao_Cmd_99_9B_9D(*args[1]);
        break;
    case 0x65:
        Akao_Cmd_98_9A_9C(*args[1]);
        break;
    case 0xC8:
    case 0x198:
        status = CdRom_SeekToTrack(*args[1], 1, 0, &locals.slot,
                                *args[2] == 0);
        if (status == 1) {
            g_ScriptCursor -= 8;
            g_ScriptNodePool->active = status;
            return 0;
        }
        if (locals.slot + 2U < 2U) {
            break;
        }
        *args[5] = Akao_Cmd_10((locals.slot == 0 ? g_GameState.bank_work_base : g_GameState.bank_work_end));
        Akao_Cmd_C0_WithSlot(0, 0x7F);
        CdRom_RegisterAudioSlot(locals.slot, *args[1], *args[5], 0x7F);
        if (g_GameState.flags & 0x8000) {
            g_MovieMusicSlots |= 1 << locals.slot;
        }
        break;
    case 0xCB:
        status = CdRom_SeekToTrack(*args[1], 1, 1, &locals.slot,
                                *args[3] == 0);
        if (status == 1) {
            g_ScriptCursor -= 8;
            g_ScriptNodePool->active = status;
            return 0;
        }
        if (locals.slot + 2U < 2U) {
            break;
        }
        *args[5] = Akao_Cmd_19_Then_C0(
            (locals.slot == 0 ? g_GameState.bank_work_base : g_GameState.bank_work_end), *args[2]);
        CdRom_RegisterAudioSlot(locals.slot, *args[1], *args[5],
                                  *args[2]);
        if (g_GameState.flags & 0x8000) {
            g_MovieMusicSlots |= 1 << locals.slot;
        }
        break;
    case 0xC9:
        locals.slot = CdRom_GetAudioSlotByKey(*args[1]);
        if (locals.slot == -1) {
            break;
        }
        Akao_Cmd_11(locals.slot);
        wait_one = 1;
        status = CdRom_SeekToTrack(*args[1], 0, 0, &locals.slot, wait_one);
        if (status != wait_one) {
            break;
        }
        g_ScriptCursor -= 8;
        g_ScriptNodePool->active = status;
        return 0;
    case 0xCC:
        Akao_Cmd_90(*args[1]);
        break;
    case 0xCD:
        locals.slot = CdRom_GetAudioSlotByKey(*args[1]);
        if (locals.slot == -1) {
            break;
        }
        Akao_Cmd_C0_WithSlot(locals.slot, *args[2]);
        slot_index0 = CdRom_FindAudioSlotIndex(*args[1]);
        handle = CdRom_GetAudioSlotByKey(*args[1]);
        CdRom_RegisterAudioSlot(slot_index0, *args[1], handle,
                                  *args[2]);
        break;
    case 0xCE:
        locals.slot = CdRom_GetAudioSlotByKey(*args[1]);
        if (locals.slot == -1) {
            break;
        }
        Akao_Cmd_C1_WithSlot(locals.slot, *args[2] * 2, *args[3]);
        slot_index1 = CdRom_FindAudioSlotIndex(*args[1]);
        handle = CdRom_GetAudioSlotByKey(*args[1]);
        CdRom_RegisterAudioSlot(slot_index1, *args[1], handle,
                                  *args[3]);
        break;
    case 0xCF:
        locals.slot = CdRom_GetAudioSlotByKey(*args[1]);
        if (locals.slot == -1) {
            break;
        }
        Akao_Cmd_C2_WithSlot(locals.slot, *args[2] * 2, *args[3],
                             *args[4]);
        slot_index2 = CdRom_FindAudioSlotIndex(*args[1]);
        handle = CdRom_GetAudioSlotByKey(*args[1]);
        CdRom_RegisterAudioSlot(slot_index2, *args[1], handle,
                                  *args[4]);
        break;
    case 0xD0:
        Akao_Cmd_D0(*args[1]);
        break;
    case 0xD1:
        Akao_Cmd_D1(*args[1] * 2, *args[2]);
        break;
    case 0xD2:
        Akao_Cmd_D2(*args[1] * 2, *args[2], *args[3]);
        break;
    case 0xD3:
        Akao_Cmd_D4(*args[1]);
        break;
    case 0xD4:
        Akao_Cmd_D5(*args[1] * 2, *args[2]);
        break;
    case 0xD5:
        Akao_Cmd_D6(*args[1] * 2, *args[2], *args[3]);
        break;
    case 0xD6:
        Akao_Cmd_D8(*args[1]);
        break;
    case 0xD7:
        Akao_Cmd_D9(*args[1] * 2, *args[2]);
        break;
    case 0xD8:
        Akao_Cmd_DA(*args[1] * 2, *args[2], *args[3]);
        break;
    case 0xD9:
        Akao_ResetPositionalState();
        break;
    case 0x12C:
        *args[5] = Akao_SendTableCommand(
            ((u8 *)g_GameState.loaded_scene_assets), *args[1], *args[2], *args[3],
            *args[4]);
        break;
    case 0x12D:
        Akao_Cmd_21(*args[1], *args[2]);
        break;
    case 0x12E:
        Akao_Cmd_A0(*args[1], *args[2], *args[3]);
        break;
    case 0x12F:
        Akao_Cmd_A1(*args[1], *args[2], *args[3] * 2,
                    *args[4]);
        break;
    case 0x130:
        Akao_Cmd_A8(*args[1]);
        break;
    case 0x131:
        Akao_Cmd_A9(*args[1] * 2, *args[2]);
        break;
    case 0x132:
        Akao_Cmd_A2(*args[1], *args[2], *args[3]);
        break;
    case 0x133:
        Akao_Cmd_A3(*args[1], *args[2], *args[3] * 8,
                    *args[4]);
        break;
    case 0x134:
        Akao_Cmd_AA(*args[1]);
        break;
    case 0x135:
        Akao_Cmd_AB(*args[1] * 2, *args[2]);
        break;
    case 0x136:
        Akao_Cmd_A4(*args[1], *args[2], *args[3]);
        break;
    case 0x137:
        Akao_Cmd_A5(*args[1], *args[2], *args[3] * 2,
                    *args[4]);
        break;
    case 0x138:
        Akao_Cmd_AC(*args[1]);
        break;
    case 0x139:
        Akao_Cmd_AD(*args[1] * 2, *args[2]);
        break;
    case 0x13A:
        Akao_Cmd_F1();
        break;
    case 0x15E:
        g_GameState.bank_value_f8 = *args[1];
        g_GameState.bank_value_f6 = *args[2];
        g_GameState.bank_value_fa = *args[3];
        g_GameState.bank_value_f7 = *args[4];
        break;
    case 0x15F:
        *args[5] = Asset_Find08w(
            *args[1], 0, Pe1Fixed_Integer(&g_CurrentEntity->pos_x),
            Pe1Fixed_Integer(&g_CurrentEntity->pos_y),
            Pe1Fixed_Integer(&g_CurrentEntity->pos_z));
        break;
    case 0x160:
        if (*args[2] == 0) {
            player = g_PlayerEntity;
            if (player == 0) {
                break;
            }
            actor = player;
        } else {
            actor = g_FieldActorListHead;
            while (actor != 0) {
                if (actor->type_id == *args[2] &&
                    actor->sub_id == *args[3] &&
                    !(actor->flags & 0x10)) {
                    break;
                }
                actor = actor->next;
            }
            if (actor == 0) {
                break;
            }
        }
        *args[5] = Asset_Find08w(
            *args[1], 0, ((Pe1Fixed16_16 *)&actor->pos_x)->parts.integer,
            ((Pe1Fixed16_16 *)&actor->pos_y)->parts.integer,
            ((Pe1Fixed16_16 *)&actor->pos_z)->parts.integer);
        break;
    case 0x161:
        *args[5] = Asset_Find08w(*args[1], 0,
                                     ((Pe1Fixed16_16 *)args[2])->parts.integer,
                                     ((Pe1Fixed16_16 *)args[3])->parts.integer,
                                     ((Pe1Fixed16_16 *)args[4])->parts.integer);
        break;
    case 0x190:
        base = ((u8 *)g_GameState.loaded_scene_assets);
        table_info = ((AkaoArchiveSection *)(base +
            ((AkaoArchiveHeader *)((u8 *)g_GameState.loaded_scene_assets))->section_offset))->asset_table;
        base += table_info & 0x3FFFFF;
        count = 0;
        entry_count = table_info >> 22;
        if (entry_count == 0) {
            break;
        }
        entry_limit = entry_count;
        entries = (AkaoAssetEntry *)base;
        do {
            current = &entries[count];
            if (current->flags & 0x10) {
                int entry_id = current->id;
                if (entry_id == *args[1]) {
                    goto archive_match;
                }
            }
        } while (++count < entry_limit);
        break;
    case 0x191:
        status = CdRom_SeekToTrack(*args[1], 1, 0, &locals.slot,
                               *args[2] == 0);
        if (status != 1) {
            *args[5] = locals.slot;
            break;
    case 0x192:
        status = CdRom_SeekToTrack(*args[1], 1, 1, &locals.slot,
                               *args[2] == 0);
        if (status == 1) {
            g_ScriptCursor -= 8;
            g_ScriptNodePool->active = status;
            return 0;
        }
        }
        else
        {
        g_ScriptCursor -= 8;
        g_ScriptNodePool->active = status;
        return 0;
        }
        *args[5] = locals.slot;
        break;
    case 0x195:
        g_GameState.bank_state_12 = 1;
        break;
    case 0x196:
    case 0x197:
        if (g_GameState.bank_state_11 < 0x10) {
            deferred = &g_DeferredAkaoCommands[g_GameState.bank_state_11];
            deferred->actor_type = g_CurrentEntity->type_id;
            deferred->actor_id = g_CurrentEntity->sub_id;
            deferred->command = *args[1];
            deferred->arg1 = *args[2];
            deferred->arg2 = *args[3];
            if (*args[0] == 0x196) {
                deferred->arg3 = *args[3];
            } else {
                deferred->arg3 = *args[4];
            }
            deferred_count = &g_GameState.bank_state_11;
            (*deferred_count)++;
        }
        break;
    case 0x199:
        volume = *args[1];
        locals.volumes[3] = 0;
        locals.volumes[1] = 0;
        g_GameState.scene_audio.tracks.movie_volume = volume;
        locals.volumes[2] = volume;
        locals.volumes[0] = volume;
        DsMix((CdlATV *)locals.volumes);
        break;
archive_match:
        g_GameState.scene_audio.tracks.pending_key = current->id;
        g_GameState.scene_audio.tracks.pending_bank = current->value8;
        g_GameState.bank_asset_table = ((u8 *)g_GameState.loaded_scene_assets) +
                                    (current->data_offset & 0xFFFFFF);
        break;
    case 0x19A:
        Akao_Cmd_92(*args[1]);
        break;
    }
    return 1;
}

/* Original resident deferral queue (16 eight-byte command records). */
DeferredAkaoCommand g_DeferredAkaoCommands[16];
