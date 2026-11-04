#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_node_types.h"
#endif
#include "common.h"
#include "pe_hooks.h"
#include "pe1/script_state.h"
#include "pe1/script_queue.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
#include "pe1/entity_animation.h"
#include "pe1/field_actor.h"
#include "pe1/gte_types.h"
#include "pe1/player_entity.h"
#include "pe1/script_node.h"

PE1_STATIC_ASSERT(sizeof(ScriptNodeTarget18) == sizeof(void *), script_node_target18_view_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(ScriptNodeTarget18, coordinate) == 0,
                  script_node_target18_coordinate_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(ScriptNodeTarget18, actor) == 0,
                  script_node_target18_actor_offset);


/* Shared native state; external variables have one storage definition. */



int Script_SetEntityActionAndWait(int **arg0) {
    ScriptNode *node = g_ScriptNodePool;
    int flags = node->flags;

    if ((flags & 0x20) == 0) {
        int *ptr;
        int mode;

        node->flags = flags | 0x20;
        ptr = arg0[0];
        {
            FieldActor *state;
            state = g_CurrentEntity;
            mode = (u16)*ptr;
            Entity_SetActionMode((BattleEntity *)state, mode);
        }
        {
            FieldActor *state;
            state = g_CurrentEntity;
            state->flags &= -0x101;
        }
    } else {
        FieldActor *state;
        s16 keep;

        state = g_CurrentEntity;
        if (state->action == 0) {
            node->flags = flags & 0xFFDF;
            return 1;
        }
        if (state->anim_step >= 0) {
            unsigned int lhs = state->anim.fixed;
            unsigned int rhs = state->anim_prev;
            int result = lhs < rhs;
            keep = result;
        } else {
            unsigned int rhs = state->anim.fixed;
            unsigned int lhs = state->anim_prev;
            int result = lhs < rhs;
            keep = result;
        }
        if (keep != 0) {
            node->flags = flags & 0xFFDF;
            return 1;
        }
    }

    {
        uintptr_t cursor = g_ScriptCursorAddress;
        ScriptNode *active_node = g_ScriptNodePool;
        cursor -= 0xC;
        g_ScriptCursorAddress = cursor;
        active_node->active = 1;
        return 0;
    }
}

/* Animation-range wait consumes the active ScriptNode and FieldActor view. */
int Script_WaitAnimRange(int **unused) {
    FieldActor *entity;
    ScriptNode *node;
    int current;
    int target;
    int next;
    int frame;
    int max_frame;
    int cmp;
    int divisor;

    entity = g_CurrentEntity;
    node = g_ScriptNodePool;
    max_frame = entity->anim.fixed;
    current = max_frame;
    target = entity->anim_frame_target;
    node->active = 1;
    if ((current >> 16) == target) {
        return 0;
    }

    target = (s32)((u32)target << 16);
    max_frame = current;
    next = max_frame + entity->anim_step;
    if (max_frame < target && target < next) {
        return 0;
    }
    cmp = target < max_frame;
    if (cmp && next < target) {
        return 0;
    }

    max_frame = g_CurrentEntity->action;
    frame = next >> 16;
    if (max_frame < frame) {
        next = (s32)((u32)(frame % (max_frame + 1)) << 16);
        cmp = target < next;
        if (cmp) {
            return 0;
        }
    } else {
        divisor = max_frame + 1;
        max_frame = (s32)((u32)max_frame << 16);
        if (next < 0) {
            /* The original frame comparison is complete; reuse its scratch. */
            current = divisor;
            next = (s32)((u32)(current + (frame % divisor)) << 16);
            if (max_frame >= target) {
                cmp = next < target;
                if (cmp) {
                    return 0;
                }
            }
        }
    }

    g_ScriptCursorAddress -= 8;
    return 0;
}

/* Point-turn and movement handlers use the same active ScriptNode layout. */
/* Script op: turn the current actor's yaw toward (x, z) by at most `step`
 * per frame along the shorter arc; yields until the yaw reaches it. */
int Script_TurnTowardPointStep(int **arg0) {
    ScriptNode *node = g_ScriptNodePool;
    int flags = node->flags;
    int x;
    int y;
    int step;
    int angle;
    int stepped;
    int current;
    int delta;

    if (!(flags & 0x20)) {
        x = *arg0[0];
        y = *arg0[1];
        step = *arg0[2];
        node->flags = flags | 0x20;
        node->target14 = x;
        node->target1c = step;
        node->target18.coordinate = y;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(node, PE_NODE_COORDINATE);
#endif
    } else {
        x = node->target14;
        y = node->target18.coordinate;
        step = node->target1c;
    }

    x = (g_CurrentEntity->pos_x - x) >> 16;
    y = (g_CurrentEntity->pos_z - y) >> 16;
    if ((x | y) == 0) {
        return 1;
    }
    angle = (0x1400 - ratan2(y, x)) & 0xFFF;
    current = g_CurrentEntity->rot_y;
    stepped = angle;
    if (angle != current) {
        if (current < angle) {
            delta = angle - current;
            if (delta < 0x800) {
                if (step < delta) {
                    stepped = current + step;
                }
            } else if (step < delta) {
                stepped = current - step;
                if (stepped < 0) {
                    int wrap = current + 0x1000;

                    wrap -= angle;
                    if (wrap < step) {
                        stepped = angle;
                    }
                }
            }
        } else {
            delta = current - angle;
            if (delta < 0x800) {
                if (step < delta) {
                    stepped = current - step;
                }
            } else if (step < delta) {
                stepped = current + step;
                if (stepped >= 0x1001) {
                    int wrap = angle + 0x1000;

                    wrap -= current;
                    if (wrap < step) {
                        stepped = angle;
                    }
                }
            }
        }
        stepped &= 0xFFF;
        g_CurrentEntity->rot_y = stepped;
        if (stepped != angle) {
            g_ScriptCursorAddress -= 0x14;
            g_ScriptNodePool->active = 1;
            return 0;
        }
    }
    g_ScriptNodePool->flags &= 0xFFDF;
    return 1;
}

int Script_MoveTowardPoint(int **args) {
    FieldActor *speed_entity;
    FieldActor *motion_entity;
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

    if ((g_ScriptNodePool->flags & 0x20) == 0) {
        target_x = *args[0];
        target_z = *args[1];
        if (current_x == target_x && current_z == target_z) {
            return 1;
        }
        turn_speed = *args[2];
        g_ScriptNodePool->target14 = target_x;
        g_ScriptNodePool->target18.coordinate = target_z;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(g_ScriptNodePool, PE_NODE_COORDINATE);
#endif
        g_ScriptNodePool->target1c = turn_speed;
        g_ScriptNodePool->flags |= 0x20;
    } else {
        camera_state = g_ScriptNodePool;
        target_x = camera_state->target14;
        target_z = camera_state->target18.coordinate;
        turn_speed = camera_state->target1c;
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

    /* Apply entry-speed options before the original distance-to-target clamp,
     * so a faster step still arrives exactly at the scripted destination. */
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
        g_ScriptNodePool->flags &= 0xFFDF;
        return 1;
    } else {
        g_ScriptCursorAddress -= 0x14;
        g_ScriptNodePool->active = 1;
        return 0;
    }
}

/* Camera entity tracking shares the active ScriptNode cache and motion ABI. */
int Render_CameraTrackEntityAngle(int **args) {
    int **script_args;
    FieldActor *current;
    FieldActor *tracking_entity;
    ScriptNode *camera_state;
    int *turn_arg;
    int base_speed;
    int current_x;
    int current_z;
    int target_z;
    int target_x;
    int target_angle;
    int turn_speed;
    int current_angle;
    int angle_difference;
    int squared_x;
    int squared_z;
    int movement_distance;

    script_args = args;
    tracking_entity = g_CurrentEntity;
    current_x = tracking_entity->pos_x;
    current_z = tracking_entity->pos_z;

    if (tracking_entity != g_PlayerEntity) {
        base_speed = tracking_entity->move_factor;
    } else {
        base_speed = Math_FixedMul(0x50000, tracking_entity->move_factor);
    }
    base_speed = Math_FixedMul(base_speed, g_CurrentEntity->move_speed << 4);

    if ((g_ScriptNodePool->flags & 0x20) == 0) {
        target_x = *script_args[0];
        target_z = *script_args[1];
        if (current_x == target_x && current_z == target_z) {
            return 1;
        }
        turn_arg = script_args[2];
        tracking_entity = g_CurrentEntity;
        turn_speed = *turn_arg;
        if (tracking_entity->rot_y < 0) {
            tracking_entity->rot_y = tracking_entity->rot_y + 0x1000;
        }

        g_ScriptNodePool->target14 = target_x;
        g_ScriptNodePool->target18.coordinate = target_z;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(g_ScriptNodePool, PE_NODE_COORDINATE);
#endif
        g_ScriptNodePool->target1c = turn_speed;
        g_ScriptNodePool->flags |= 0x20;
    } else {
        target_x = g_ScriptNodePool->target14;
        target_z = g_ScriptNodePool->target18.coordinate;
        turn_speed = g_ScriptNodePool->target1c;
    }

    target_angle = (u16)(0x1400 - ratan2((s32)((u32)current_z - (u32)target_z), (s32)((u32)current_x - (u32)target_x)));
    target_angle &= 0xFFF;
    PE_HOOK(PE_HOOK_ACTOR_STEP, (&(PeHookActorStep){tracking_entity, &base_speed, NULL}));
    g_CurrentEntity->motion_x = Math_FixedMul((s32)(0u - (u32)base_speed), (s32)((u32)rsin(target_angle) << 4));
    base_speed = Math_FixedMul((s32)(0u - (u32)base_speed), (s32)((u32)rcos(target_angle) << 4));

    current = g_CurrentEntity;
    angle_difference = current->rot_y;
    current_angle = angle_difference;
    current->motion_z = base_speed;
    if (current_angle < target_angle) {
        angle_difference = target_angle - current_angle;
        if (angle_difference < 0x801) {
            if (turn_speed < angle_difference) {
                current->rot_y = (s32)((u32)current_angle + (u32)turn_speed);
            }
        } else if (turn_speed < angle_difference) {
            current->rot_y = (s32)((u32)current_angle - (u32)turn_speed);
        }
    } else {
        angle_difference = current_angle - target_angle;
        if (angle_difference < 0x801) {
            if (turn_speed < angle_difference) {
                current->rot_y = (s32)((u32)current_angle - (u32)turn_speed);
            }
        } else if (turn_speed < angle_difference) {
            current->rot_y = (s32)((u32)current_angle + (u32)turn_speed);
        }
    }

    current = g_CurrentEntity;
    current_x = (s32)((u32)target_x - (u32)current_x) >> 16;
    current_z = (s32)((u32)target_z - (u32)current_z) >> 16;
    squared_x = current_x * current_x;
    squared_z = current_z * current_z;
    angle_difference = (s32)((u32)squared_x + (u32)squared_z);
    current_x = Pe1Fixed_Integer(&current->motion_x);
    current_z = Pe1Fixed_Integer(&current->motion_z);
    movement_distance = (s32)((u32)(current_x * current_x) + (u32)(current_z * current_z));
    current->rot_y &= 0xFFF;
    if (movement_distance >= angle_difference) {
        camera_state = g_ScriptNodePool;
        current->pos_x = target_x;
        current->pos_z = target_z;
        current->motion_x = 0;
        current->motion_z = 0;
        camera_state->flags &= 0xFFDF;
        return 1;
    } else {
        g_ScriptCursorAddress -= 0x14;
        g_ScriptNodePool->active = 1;
        return 0;
    }
}

int Render_CameraSnapToEntity(int **args) {
    FieldActor *target;
    int speed;
    int target_angle;
    int next_angle;
    int angle;
    int difference;
    int wrap_difference;
    int delta_x;
    int delta_z;

    if ((g_ScriptNodePool->flags & 0x20) == 0) {
        if (*args[0] == 0) {
            if (g_PlayerEntity == 0) {
                return 1;
            }
            target = g_PlayerEntity;
        } else {
            target = g_FieldActorListHead;
            while (target != 0) {
                if (target->type_id == *args[0] && target->sub_id == *args[1] &&
                    (target->flags & 0x10) == 0) {
                    break;
                }
                target = target->next;
            }
            if (target == 0) {
                return 1;
            }
        }

        speed = *args[2];
        g_ScriptNodePool->target18.actor = target;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(g_ScriptNodePool, PE_NODE_ACTOR);
#endif
        g_ScriptNodePool->flags |= 0x20;
        g_ScriptNodePool->target14 = speed;
    } else {
        target = g_ScriptNodePool->target18.actor;
        if ((target->flags & 0x10) != 0) {
            g_ScriptNodePool->flags &= 0xFFDF;
            return 1;
        }
        speed = g_ScriptNodePool->target14;
    }

    delta_x = (s32)((u32)g_CurrentEntity->pos_x - (u32)target->pos_x);
    delta_z = (s32)((u32)g_CurrentEntity->pos_z - (u32)target->pos_z);
    target_angle = 0x1400 - ratan2(delta_z >> 16, delta_x >> 16);
    next_angle = (target_angle &= 0xFFF);
    angle = g_CurrentEntity->rot_y;
    if (target_angle != angle) {
        if (angle < target_angle) {
            difference = target_angle - angle;
            if (difference < 0x800) {
                if (speed < difference) {
                    next_angle = angle + speed;
                }
            } else if (speed < difference) {
                next_angle = angle - speed;
                if (next_angle < 0) {
                    wrap_difference = angle + 0x1000;
                    wrap_difference -= target_angle;
                    if (wrap_difference < speed) {
                        next_angle = target_angle;
                    }
                }
            }
        } else {
            difference = angle - target_angle;
            if (difference < 0x800) {
                if (speed < difference) {
                    next_angle = angle - speed;
                }
            } else if (speed < difference) {
                next_angle = angle + speed;
                if (next_angle >= 0x1001) {
                    wrap_difference = target_angle + 0x1000;
                    wrap_difference -= angle;
                    if (wrap_difference < speed) {
                        next_angle = target_angle;
                    }
                }
            }
        }

        next_angle &= 0xFFF;
        g_CurrentEntity->rot_y = next_angle;
        if (next_angle != target_angle) {
            g_ScriptCursorAddress -= 0x14;
            g_ScriptNodePool->active = 1;
            return 0;
        }
    }

    g_ScriptNodePool->flags &= 0xFFDF;
    return 1;
}

int Render_CameraTrackEntityZoom(int **args) {
    FieldActor *speed_entity;
    FieldActor *motion_entity;
    FieldActor *target;
    ScriptNode *camera_state;
    int target_move_id;
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

    if ((g_ScriptNodePool->flags & 0x20) == 0) {
        if (*args[0] == 0) {
            if (g_PlayerEntity == 0) {
                return 1;
            }
            target = g_PlayerEntity;
        } else {
            target = g_FieldActorListHead;
            while (target != 0) {
                if (target->type_id == *args[0] && target->sub_id == *args[1] &&
                    (target->flags & 0x10) == 0) {
                    break;
                }
                target = target->next;
            }
            if (target == 0) {
                return 1;
            }
        }

        g_ScriptNodePool->flags |= 0x20;
        turn_speed = *args[2];
        g_ScriptNodePool->target18.actor = target;
#if PE_NATIVE_STATE_CPU_COMPONENTS

        PeStateNode_Tag(g_ScriptNodePool, PE_NODE_ACTOR);
#endif
        g_ScriptNodePool->target14 = turn_speed;
        target_move_id = target->field_sfx_id;
        g_ScriptNodePool->target1c = target_move_id;
    } else {
        target = g_ScriptNodePool->target18.actor;
        turn_speed = g_ScriptNodePool->target14;
        if ((target->flags & 0x10) != 0 ||
            target->field_sfx_id != g_ScriptNodePool->target1c) {
            g_ScriptNodePool->flags &= 0xFFDF;
            return 1;
        }
    }

    target_x = target->pos_x;
    target_z = target->pos_z;
    current_x = g_CurrentEntity->pos_x;
    current_z = g_CurrentEntity->pos_z;
    if (current_x == target_x && current_z == target_z) {
        return 1;
    }

    speed_entity = g_CurrentEntity;
    if (speed_entity != g_PlayerEntity) {
        movement_speed = speed_entity->move_factor;
    } else {
        movement_speed = Math_FixedMul(0x50000, speed_entity->move_factor);
    }
    movement_speed = Math_FixedMul(movement_speed, g_CurrentEntity->move_speed << 4);
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
        g_ScriptCursorAddress -= 0x14;
        g_ScriptNodePool->active = 1;
        return 0;
    }
}
